//! Lower-level Server connection API.
//!
//! The types in this module are to provide a lower-level API based around a
//! single connection. Accepting a connection and binding it with a service
//! are not handled at this level. This module provides the building blocks to
//! customize those things externally.
//!
//! If you don't have need to manage connections yourself, consider using the
//! higher-level [Server](super) API.

use std::fmt;
use std::mem;
#[cfg(feature = "runtime")] use std::net::SocketAddr;
use std::sync::Arc;
#[cfg(feature = "runtime")] use std::time::Duration;

use bytes::Bytes;
use futures::{Async, Future, Poll, Stream};
use futures::future::{Either, Executor};
use tokio_io::{AsyncRead, AsyncWrite};
#[cfg(feature = "runtime")] use tokio_reactor::Handle;

use body::{Body, Payload};
use common::Exec;
use common::io::Rewind;
use error::{Kind, Parse};
use proto;
use service::{NewService, Service};
use upgrade::Upgraded;

use self::upgrades::UpgradeableConnection;

#[cfg(feature = "runtime")] pub use super::tcp::AddrIncoming;

/// A lower-level configuration of the HTTP protocol.
///
/// This structure is used to configure options for an HTTP server connection.
///
/// If you don't have need to manage connections yourself, consider using the
/// higher-level [Server](super) API.
#[derive(Clone, Debug)]
pub struct Http {
    exec: Exec,
    h1_writev: bool,
    mode: ConnectionMode,
    keep_alive: bool,
    max_buf_size: Option<usize>,
    pipeline_flush: bool,
}

/// The internal mode of HTTP protocol which indicates the behavior when a parse error occurs.
#[derive(Clone, Debug, PartialEq)]
enum ConnectionMode {
    /// Always use HTTP/1 and do not upgrade when a parse error occurs.
    H1Only,
    /// Always use HTTP/2.
    H2Only,
    /// Use HTTP/1 and try to upgrade to h2 when a parse error occurs.
    Fallback,
}

/// A stream mapping incoming IOs to new services.
///
/// Yields `Connecting`s that are futures that should be put on a reactor.
#[must_use = "streams do nothing unless polled"]
#[derive(Debug)]
pub struct Serve<I, S> {
    incoming: I,
    new_service: S,
    protocol: Http,
}

/// A future building a new `Service` to a `Connection`.
///
/// Wraps the future returned from `NewService` into one that returns
/// a `Connection`.
#[must_use = "futures do nothing unless polled"]
#[derive(Debug)]
pub struct Connecting<I, F> {
    future: F,
    io: Option<I>,
    protocol: Http,
}

#[must_use = "futures do nothing unless polled"]
#[derive(Debug)]
pub(super) struct SpawnAll<I, S> {
    serve: Serve<I, S>,
}

/// A future binding a connection with a Service.
///
/// Polling this future will drive HTTP forward.
#[must_use = "futures do nothing unless polled"]
pub struct Connection<T, S>
where
    S: Service,
{
    pub(super) conn: Option<
        Either<
        proto::h1::Dispatcher<
            proto::h1::dispatch::Server<S>,
            S::ResBody,
            T,
            proto::ServerTransaction,
        >,
        proto::h2::Server<
            Rewind<T>,
            S,
            S::ResBody,
        >,
    >>,
    fallback: bool,
}



/// Deconstructed parts of a `Connection`.
///
/// This allows taking apart a `Connection` at a later time, in order to
/// reclaim the IO object, and additional related pieces.
#[derive(Debug)]
pub struct Parts<T, S>  {
    /// The original IO object used in the handshake.
    pub io: T,
    /// A buffer of bytes that have been read but not processed as HTTP.
    ///
    /// If the client sent additional bytes after its last request, and
    /// this connection "ended" with an upgrade, the read buffer will contain
    /// those bytes.
    ///
    /// You will want to check for any existing bytes if you plan to continue
    /// communicating on the IO object.
    pub read_buf: Bytes,
    /// The `Service` used to serve this connection.
    pub service: S,
    _inner: (),
}

// ===== impl Http =====

impl Http {
    /// Creates a new instance of the HTTP protocol, ready to spawn a server or
    /// start accepting connections.
    pub fn new() -> Http {
        Http {
            exec: Exec::Default,
            h1_writev: true,
            mode: ConnectionMode::Fallback,
            keep_alive: true,
            max_buf_size: None,
            pipeline_flush: false,
        }
    }

    /// Sets whether HTTP1 is required.
    ///
    /// Default is false
    pub fn http1_only(&mut self, val: bool) -> &mut Self {
        if val {
            self.mode = ConnectionMode::H1Only;
        } else {
            self.mode = ConnectionMode::Fallback;
        }
        self
    }

    /// Set whether HTTP/1 connections should try to use vectored writes,
    /// or always flatten into a single buffer.
    ///
    /// Note that setting this to false may mean more copies of body data,
    /// but may also improve performance when an IO transport doesn't
    /// support vectored writes well, such as most TLS implementations.
    ///
    /// Default is `true`.
    #[inline]
    pub fn http1_writev(&mut self, val: bool) -> &mut Self {
        self.h1_writev = val;
        self
    }

    /// Sets whether HTTP2 is required.
    ///
    /// Default is false
    pub fn http2_only(&mut self, val: bool) -> &mut Self {
        if val {
            self.mode = ConnectionMode::H2Only;
        } else {
            self.mode = ConnectionMode::Fallback;
        }
        self
    }

    /// Enables or disables HTTP keep-alive.
    ///
    /// Default is true.
    pub fn keep_alive(&mut self, val: bool) -> &mut Self {
        self.keep_alive = val;
        self
    }

    /// Set the maximum buffer size for the connection.
    ///
    /// Default is ~400kb.
    ///
    /// # Panics
    ///
    /// The minimum value allowed is 8192. This method panics if the passed `max` is less than the minimum.
    pub fn max_buf_size(&mut self, max: usize) -> &mut Self {
        assert!(
            max >= proto::h1::MINIMUM_MAX_BUFFER_SIZE,
            "the max_buf_size cannot be smaller than the minimum that h1 specifies."
        );
        self.max_buf_size = Some(max);
        self
    }

    /// Aggregates flushes to better support pipelined responses.
    ///
    /// Experimental, may have bugs.
    ///
    /// Default is false.
    pub fn pipeline_flush(&mut self, enabled: bool) -> &mut Self {
        self.pipeline_flush = enabled;
        self
    }

    /// Set the executor used to spawn background tasks.
    ///
    /// Default uses implicit default (like `tokio::spawn`).
    pub fn executor<E>(&mut self, exec: E) -> &mut Self
    where
        E: Executor<Box<Future<Item=(), Error=()> + Send>> + Send + Sync + 'static
    {
        self.exec = Exec::Executor(Arc::new(exec));
        self
    }

    /// Bind a connection together with a [`Service`](::service::Service).
    ///
    /// This returns a Future that must be polled in order for HTTP to be
    /// driven on the connection.
    ///
    /// # Example
    ///
    /// ```
    /// # extern crate hyper;
    /// # extern crate tokio_io;
    /// # #[cfg(feature = "runtime")]
    /// # extern crate tokio;
    /// # use hyper::{Body, Request, Response};
    /// # use hyper::service::Service;
    /// # use hyper::server::conn::Http;
    /// # use tokio_io::{AsyncRead, AsyncWrite};
    /// # #[cfg(feature = "runtime")]
    /// # fn run<I, S>(some_io: I, some_service: S)
    /// # where
    /// #     I: AsyncRead + AsyncWrite + Send + 'static,
    /// #     S: Service<ReqBody=Body, ResBody=Body> + Send + 'static,
    /// #     S::Future: Send
    /// # {
    /// # use hyper::rt::Future;
    /// # use tokio::reactor::Handle;
    /// let http = Http::new();
    /// let conn = http.serve_connection(some_io, some_service);
    ///
    /// let fut = conn.map_err(|e| {
    ///     eprintln!("server connection error: {}", e);
    /// });
    ///
    /// hyper::rt::spawn(fut);
    /// # }
    /// # fn main() {}
    /// ```
    pub fn serve_connection<S, I, Bd>(&self, io: I, service: S) -> Connection<I, S>
    where
        S: Service<ReqBody=Body, ResBody=Bd>,
        S::Error: Into<Box<::std::error::Error + Send + Sync>>,
        S::Future: Send + 'static,
        Bd: Payload,
        I: AsyncRead + AsyncWrite,
    {
        let either = match self.mode {
            ConnectionMode::H1Only | ConnectionMode::Fallback => {
                let mut conn = proto::Conn::new(io);
                if !self.keep_alive {
                    conn.disable_keep_alive();
                }
                if !self.h1_writev {
                    conn.set_write_strategy_flatten();
                }
                conn.set_flush_pipeline(self.pipeline_flush);
                if let Some(max) = self.max_buf_size {
                    conn.set_max_buf_size(max);
                }
                let sd = proto::h1::dispatch::Server::new(service);
                Either::A(proto::h1::Dispatcher::new(sd, conn))
            }
            ConnectionMode::H2Only => {
                let rewind_io = Rewind::new(io);
                let h2 = proto::h2::Server::new(rewind_io, service, self.exec.clone());
                Either::B(h2)
            }
        };

        Connection {
            conn: Some(either),
            fallback: self.mode == ConnectionMode::Fallback,
        }
    }

    /// Bind the provided `addr` with the default `Handle` and return [`Serve`](Serve).
    ///
    /// This method will bind the `addr` provided with a new TCP listener ready
    /// to accept connections. Each connection will be processed with the
    /// `new_service` object provided, creating a new service per
    /// connection.
    #[cfg(feature = "runtime")]
    pub fn serve_addr<S, Bd>(&self, addr: &SocketAddr, new_service: S) -> ::Result<Serve<AddrIncoming, S>>
    where
        S: NewService<ReqBody=Body, ResBody=Bd>,
        S::Error: Into<Box<::std::error::Error + Send + Sync>>,
        Bd: Payload,
    {
        let mut incoming = AddrIncoming::new(addr, None)?;
        if self.keep_alive {
            incoming.set_keepalive(Some(Duration::from_secs(90)));
        }
        Ok(self.serve_incoming(incoming, new_service))
    }

    /// Bind the provided `addr` with the `Handle` and return a [`Serve`](Serve)
    ///
    /// This method will bind the `addr` provided with a new TCP listener ready
    /// to accept connections. Each connection will be processed with the
    /// `new_service` object provided, creating a new service per
    /// connection.
    #[cfg(feature = "runtime")]
    pub fn serve_addr_handle<S, Bd>(&self, addr: &SocketAddr, handle: &Handle, new_service: S) -> ::Result<Serve<AddrIncoming, S>>
    where
        S: NewService<ReqBody=Body, ResBody=Bd>,
        S::Error: Into<Box<::std::error::Error + Send + Sync>>,
        Bd: Payload,
    {
        let mut incoming = AddrIncoming::new(addr, Some(handle))?;
        if self.keep_alive {
            incoming.set_keepalive(Some(Duration::from_secs(90)));
        }
        Ok(self.serve_incoming(incoming, new_service))
    }

    /// Bind the provided stream of incoming IO objects with a `NewService`.
    pub fn serve_incoming<I, S, Bd>(&self, incoming: I, new_service: S) -> Serve<I, S>
    where
        I: Stream,
        I::Error: Into<Box<::std::error::Error + Send + Sync>>,
        I::Item: AsyncRead + AsyncWrite,
        S: NewService<ReqBody=Body, ResBody=Bd>,
        S::Error: Into<Box<::std::error::Error + Send + Sync>>,
        Bd: Payload,
    {
        Serve {
            incoming: incoming,
            new_service: new_service,
            protocol: self.clone(),
        }
    }
}


// ===== impl Connection =====

impl<I, B, S> Connection<I, S>
where
    S: Service<ReqBody=Body, ResBody=B> + 'static,
    S::Error: Into<Box<::std::error::Error + Send + Sync>>,
    S::Future: Send,
    I: AsyncRead + AsyncWrite + 'static,
    B: Payload + 'static,
{
    /// Start a graceful shutdown process for this connection.
    ///
    /// This `Connection` should continue to be polled until shutdown
    /// can finish.
    pub fn graceful_shutdown(&mut self) {
        match *self.conn.as_mut().unwrap() {
            Either::A(ref mut h1) => {
                h1.disable_keep_alive();
            },
            Either::B(ref mut h2) => {
                h2.graceful_shutdown();
            }
        }
    }

    /// Return the inner IO object, and additional information.
    ///
    /// If the IO object has been "rewound" the io will not contain those bytes rewound.
    /// This should only be called after `poll_without_shutdown` signals
    /// that the connection is "done". Otherwise, it may not have finished
    /// flushing all necessary HTTP bytes.
    ///
    /// # Panics
    /// This method will panic if this connection is using an h2 protocol.
    pub fn into_parts(self) -> Parts<I, S> {
        self.try_into_parts().unwrap_or_else(|| panic!("h2 cannot into_inner"))
    }

    /// Return the inner IO object, and additional information, if available.
    ///
    /// This method will return a `None` if this connection is using an h2 protocol.
    pub fn try_into_parts(self) -> Option<Parts<I, S>> {
        match self.conn.unwrap() {
            Either::A(h1) => {
                let (io, read_buf, dispatch) = h1.into_inner();
                Some(Parts {
                    io: io,
                    read_buf: read_buf,
                    service: dispatch.into_service(),
                    _inner: (),
                })
            },
            Either::B(_h2) => None,
        }
    }

    /// Poll the connection for completion, but without calling `shutdown`
    /// on the underlying IO.
    ///
    /// This is useful to allow running a connection while doing an HTTP
    /// upgrade. Once the upgrade is completed, the connection would be "done",
    /// but it is not desired to actally shutdown the IO object. Instead you
    /// would take it back using `into_parts`.
    pub fn poll_without_shutdown(&mut self) -> Poll<(), ::Error> {
        loop {
            let polled = match *self.conn.as_mut().unwrap() {
                Either::A(ref mut h1) => h1.poll_without_shutdown(),
                Either::B(ref mut h2) => return h2.poll().map(|x| x.map(|_| ())),
            };
            match polled {
                Ok(x) => return Ok(x),
                Err(e) => {
                    debug!("error polling connection protocol without shutdown: {}", e);
                    match *e.kind() {
                        Kind::Parse(Parse::VersionH2) if self.fallback => {
                            self.upgrade_h2();
                            continue;
                        }
                        _ => return Err(e),
                    }
                }
            }
        }
    }

    fn upgrade_h2(&mut self) {
        trace!("Trying to upgrade connection to h2");
        let conn = self.conn.take();

        let (io, read_buf, dispatch) = match conn.unwrap() {
            Either::A(h1) => {
                h1.into_inner()
            },
            Either::B(_h2) => {
                panic!("h2 cannot into_inner");
            }
        };
        let mut rewind_io = Rewind::new(io);
        rewind_io.rewind(read_buf);
        let h2 = proto::h2::Server::new(rewind_io, dispatch.into_service(), Exec::Default);

        debug_assert!(self.conn.is_none());
        self.conn = Some(Either::B(h2));
    }

    /// Enable this connection to support higher-level HTTP upgrades.
    ///
    /// See [the `upgrade` module](::upgrade) for more.
    pub fn with_upgrades(self) -> UpgradeableConnection<I, S>
    where
        I: Send,
    {
        UpgradeableConnection {
            inner: self,
        }
    }
}

impl<I, B, S> Future for Connection<I, S>
where
    S: Service<ReqBody=Body, ResBody=B> + 'static,
    S::Error: Into<Box<::std::error::Error + Send + Sync>>,
    S::Future: Send,
    I: AsyncRead + AsyncWrite + 'static,
    B: Payload + 'static,
{
    type Item = ();
    type Error = ::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        loop {
            match self.conn.poll() {
                Ok(x) => return Ok(x.map(|opt| {
                    if let Some(proto::Dispatched::Upgrade(pending)) = opt {
                        // With no `Send` bound on `I`, we can't try to do
                        // upgrades here. In case a user was trying to use
                        // `Body::on_upgrade` with this API, send a special
                        // error letting them know about that.
                        pending.manual();
                    }
                })),
                Err(e) => {
                    debug!("error polling connection protocol: {}", e);
                    match *e.kind() {
                        Kind::Parse(Parse::VersionH2) if self.fallback => {
                            self.upgrade_h2();
                            continue;
                        }
                        _ => return Err(e),
                    }
                }
            }
        }
    }
}

impl<I, S> fmt::Debug for Connection<I, S>
where
    S: Service,
{
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("Connection")
            .finish()
    }
}
// ===== impl Serve =====

impl<I, S> Serve<I, S> {
    /// Spawn all incoming connections onto the executor in `Http`.
    pub(super) fn spawn_all(self) -> SpawnAll<I, S> {
        SpawnAll {
            serve: self,
        }
    }

    /// Get a reference to the incoming stream.
    #[inline]
    pub fn incoming_ref(&self) -> &I {
        &self.incoming
    }

    /// Get a mutable reference to the incoming stream.
    #[inline]
    pub fn incoming_mut(&mut self) -> &mut I {
        &mut self.incoming
    }
}

impl<I, S, B> Stream for Serve<I, S>
where
    I: Stream,
    I::Item: AsyncRead + AsyncWrite,
    I::Error: Into<Box<::std::error::Error + Send + Sync>>,
    S: NewService<ReqBody=Body, ResBody=B>,
    S::Error: Into<Box<::std::error::Error + Send + Sync>>,
    <S::Service as Service>::Future: Send + 'static,
    B: Payload,
{
    type Item = Connecting<I::Item, S::Future>;
    type Error = ::Error;

    fn poll(&mut self) -> Poll<Option<Self::Item>, Self::Error> {
        if let Some(io) = try_ready!(self.incoming.poll().map_err(::Error::new_accept)) {
            let new_fut = self.new_service.new_service();
            Ok(Async::Ready(Some(Connecting {
                future: new_fut,
                io: Some(io),
                protocol: self.protocol.clone(),
            })))
        } else {
            Ok(Async::Ready(None))
        }
    }
}

// ===== impl Connecting =====

impl<I, F, S, B> Future for Connecting<I, F>
where
    I: AsyncRead + AsyncWrite,
    F: Future<Item=S>,
    S: Service<ReqBody=Body, ResBody=B>,
    S::Future: Send + 'static,
    B: Payload,
{
    type Item = Connection<I, S>;
    type Error = F::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        let service = try_ready!(self.future.poll());
        let io = self.io.take().expect("polled after complete");
        Ok(self.protocol.serve_connection(io, service).into())
    }
}

// ===== impl SpawnAll =====

#[cfg(feature = "runtime")]
impl<S> SpawnAll<AddrIncoming, S> {
    pub(super) fn local_addr(&self) -> SocketAddr {
        self.serve.incoming.local_addr()
    }
}

impl<I, S> SpawnAll<I, S> {
    pub(super) fn incoming_ref(&self) -> &I {
        self.serve.incoming_ref()
    }
}

impl<I, S, B> Future for SpawnAll<I, S>
where
    I: Stream,
    I::Error: Into<Box<::std::error::Error + Send + Sync>>,
    I::Item: AsyncRead + AsyncWrite + Send + 'static,
    S: NewService<ReqBody=Body, ResBody=B> + Send + 'static,
    S::Error: Into<Box<::std::error::Error + Send + Sync>>,
    S::Service: Send,
    S::Future: Send + 'static,
    <S::Service as Service>::Future: Send + 'static,
    B: Payload,
{
    type Item = ();
    type Error = ::Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        loop {
            if let Some(connecting) = try_ready!(self.serve.poll()) {
                let fut = connecting
                    .map_err(::Error::new_user_new_service)
                    // flatten basically
                    .and_then(|conn| conn.with_upgrades())
                    .map_err(|err| debug!("conn error: {}", err));
                self.serve.protocol.exec.execute(fut)?;
            } else {
                return Ok(Async::Ready(()))
            }
        }
    }
}

mod upgrades {
    use super::*;

    // A future binding a connection with a Service with Upgrade support.
    //
    // This type is unnameable outside the crate, and so basically just an
    // `impl Future`, without requiring Rust 1.26.
    #[must_use = "futures do nothing unless polled"]
    #[allow(missing_debug_implementations)]
    pub struct UpgradeableConnection<T, S>
    where
        S: Service,
    {
        pub(super) inner: Connection<T, S>,
    }

    impl<I, B, S> UpgradeableConnection<I, S>
    where
        S: Service<ReqBody=Body, ResBody=B> + 'static,
        S::Error: Into<Box<::std::error::Error + Send + Sync>>,
        S::Future: Send,
        I: AsyncRead + AsyncWrite + Send + 'static,
        B: Payload + 'static,
    {
        /// Start a graceful shutdown process for this connection.
        ///
        /// This `Connection` should continue to be polled until shutdown
        /// can finish.
        pub fn graceful_shutdown(&mut self) {
            self.inner.graceful_shutdown()
        }
    }

    impl<I, B, S> Future for UpgradeableConnection<I, S>
    where
        S: Service<ReqBody=Body, ResBody=B> + 'static,
        S::Error: Into<Box<::std::error::Error + Send + Sync>>,
        S::Future: Send,
        I: AsyncRead + AsyncWrite + Send + 'static,
        B: Payload + 'static,
    {
        type Item = ();
        type Error = ::Error;

        fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
            loop {
                match self.inner.conn.poll() {
                    Ok(Async::NotReady) => return Ok(Async::NotReady),
                    Ok(Async::Ready(Some(proto::Dispatched::Shutdown))) |
                    Ok(Async::Ready(None)) => {
                        return Ok(Async::Ready(()));
                    },
                    Ok(Async::Ready(Some(proto::Dispatched::Upgrade(pending)))) => {
                        let h1 = match mem::replace(&mut self.inner.conn, None) {
                            Some(Either::A(h1)) => h1,
                            _ => unreachable!("Upgrade expects h1"),
                        };

                        let (io, buf, _) = h1.into_inner();
                        pending.fulfill(Upgraded::new(Box::new(io), buf));
                        return Ok(Async::Ready(()));
                    },
                    Err(e) => {
                        debug!("error polling connection protocol: {}", e);
                        match *e.kind() {
                            Kind::Parse(Parse::VersionH2) if self.inner.fallback => {
                                self.inner.upgrade_h2();
                                continue;
                            }
                            _ => return Err(e),
                        }
                    }
                }
            }
        }
    }
}

