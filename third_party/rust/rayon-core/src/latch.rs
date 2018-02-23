use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Mutex, Condvar};
use std::usize;

use sleep::Sleep;

/// We define various kinds of latches, which are all a primitive signaling
/// mechanism. A latch starts as false. Eventually someone calls `set()` and
/// it becomes true. You can test if it has been set by calling `probe()`.
///
/// Some kinds of latches, but not all, support a `wait()` operation
/// that will wait until the latch is set, blocking efficiently. That
/// is not part of the trait since it is not possibly to do with all
/// latches.
///
/// The intention is that `set()` is called once, but `probe()` may be
/// called any number of times. Once `probe()` returns true, the memory
/// effects that occurred before `set()` become visible.
///
/// It'd probably be better to refactor the API into two paired types,
/// but that's a bit of work, and this is not a public API.
///
/// ## Memory ordering
///
/// Latches need to guarantee two things:
///
/// - Once `probe()` returns true, all memory effects from the `set()`
///   are visible (in other words, the set should synchronize-with
///   the probe).
/// - Once `set()` occurs, the next `probe()` *will* observe it.  This
///   typically requires a seq-cst ordering. See [the "tickle-then-get-sleepy" scenario in the sleep
///   README](/src/sleep/README.md#tickle-then-get-sleepy) for details.
pub trait Latch: LatchProbe {
    /// Set the latch, signalling others.
    fn set(&self);
}

pub trait LatchProbe {
    /// Test if the latch is set.
    fn probe(&self) -> bool;
}

pub struct AtomicBoolPreserved {
    data: Mutex<AtomicBool>,
}

impl AtomicBoolPreserved {
    pub fn new(v: bool) -> AtomicBoolPreserved {
        AtomicBoolPreserved {
            data: Mutex::new(AtomicBool::new(v)),
        }
    }

    pub fn load(&self, order: Ordering) -> bool {
        self.data.lock().unwrap().load(order)
    }

    pub fn store(&self, v: bool, order: Ordering) {
        self.data.lock().unwrap().store(v, order)
    }

    pub fn swap(&self, ptr: bool, order: Ordering) -> bool {
        self.data.lock().unwrap().swap(ptr, order)
    }

    pub fn compare_exchange(&self,
                            current: bool,
                            new: bool,
                            success: Ordering,
                            failure: Ordering)
                            -> Result<bool, bool> {
        self.data.lock().unwrap().compare_exchange(current, new, success, failure)
    }
}

/// Spin latches are the simplest, most efficient kind, but they do
/// not support a `wait()` operation. They just have a boolean flag
/// that becomes true when `set()` is called.
pub struct SpinLatch {
    b: AtomicBoolPreserved,
}

impl SpinLatch {
    #[inline]
    pub fn new() -> SpinLatch {
        SpinLatch { b: AtomicBoolPreserved::new(false) }
    }
}

impl LatchProbe for SpinLatch {
    #[inline]
    fn probe(&self) -> bool {
        self.b.load(Ordering::SeqCst)
    }
}

impl Latch for SpinLatch {
    #[inline]
    fn set(&self) {
        self.b.store(true, Ordering::SeqCst);
    }
}

/// A Latch starts as false and eventually becomes true. You can block
/// until it becomes true.
pub struct LockLatch {
    m: Mutex<bool>,
    v: Condvar,
}

impl LockLatch {
    #[inline]
    pub fn new() -> LockLatch {
        LockLatch {
            m: Mutex::new(false),
            v: Condvar::new(),
        }
    }

    /// Block until latch is set.
    pub fn wait(&self) {
        let mut guard = self.m.lock().unwrap();
        while !*guard {
            guard = self.v.wait(guard).unwrap();
        }
    }
}

impl LatchProbe for LockLatch {
    #[inline]
    fn probe(&self) -> bool {
        // Not particularly efficient, but we don't really use this operation
        let guard = self.m.lock().unwrap();
        *guard
    }
}

impl Latch for LockLatch {
    #[inline]
    fn set(&self) {
        let mut guard = self.m.lock().unwrap();
        *guard = true;
        self.v.notify_all();
    }
}

#[derive(Debug)]
pub struct AtomicUsizePreserved {
    data: Mutex<AtomicUsize>,
}

impl AtomicUsizePreserved {
    pub fn new(v: usize) -> AtomicUsizePreserved {
        AtomicUsizePreserved {
            data: Mutex::new(AtomicUsize::new(v)),
        }
    }

    pub fn load(&self, order: Ordering) -> usize {
        self.data.lock().unwrap().load(order)
    }

    pub fn fetch_add(&self, v: usize, order: Ordering) -> usize {
        self.data.lock().unwrap().fetch_add(v, order)
    }

    pub fn fetch_sub(&self, v: usize, order: Ordering) -> usize {
        self.data.lock().unwrap().fetch_sub(v, order)
    }

    pub fn swap(&self, ptr: usize, order: Ordering) -> usize {
        self.data.lock().unwrap().swap(ptr, order)
    }

    pub fn compare_exchange(&self,
                            current: usize,
                            new: usize,
                            success: Ordering,
                            failure: Ordering)
                            -> Result<usize, usize> {
        self.data.lock().unwrap().compare_exchange(current, new, success, failure)
    }
}

/// Counting latches are used to implement scopes. They track a
/// counter. Unlike other latches, calling `set()` does not
/// necessarily make the latch be considered `set()`; instead, it just
/// decrements the counter. The latch is only "set" (in the sense that
/// `probe()` returns true) once the counter reaches zero.
#[derive(Debug)]
pub struct CountLatch {
    counter: AtomicUsizePreserved,
}

impl CountLatch {
    #[inline]
    pub fn new() -> CountLatch {
        CountLatch { counter: AtomicUsizePreserved::new(1) }
    }

    #[inline]
    pub fn increment(&self) {
        debug_assert!(!self.probe());
        self.counter.fetch_add(1, Ordering::Relaxed);
    }
}

impl LatchProbe for CountLatch {
    #[inline]
    fn probe(&self) -> bool {
        // Need to acquire any memory reads before latch was set:
        self.counter.load(Ordering::SeqCst) == 0
    }
}

impl Latch for CountLatch {
    /// Set the latch to true, releasing all threads who are waiting.
    #[inline]
    fn set(&self) {
        self.counter.fetch_sub(1, Ordering::SeqCst);
    }
}


/// A tickling latch wraps another latch type, and will also awaken a thread
/// pool when it is set.  This is useful for jobs injected between thread pools,
/// so the source pool can continue processing its own work while waiting.
pub struct TickleLatch<'a, L: Latch> {
    inner: L,
    sleep: &'a Sleep,
}

impl<'a, L: Latch> TickleLatch<'a, L> {
    #[inline]
    pub fn new(latch: L, sleep: &'a Sleep) -> Self {
        TickleLatch {
            inner: latch,
            sleep: sleep,
        }
    }
}

impl<'a, L: Latch> LatchProbe for TickleLatch<'a, L> {
    #[inline]
    fn probe(&self) -> bool {
        self.inner.probe()
    }
}

impl<'a, L: Latch> Latch for TickleLatch<'a, L> {
    #[inline]
    fn set(&self) {
        self.inner.set();
        self.sleep.tickle(usize::MAX);
    }
}
