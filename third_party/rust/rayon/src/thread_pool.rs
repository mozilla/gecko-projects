use Configuration;
use deque;
use deque::{Worker, Stealer, Stolen};
use job::{JobRef, JobMode};
use latch::{Latch, LockLatch, SpinLatch};
#[allow(unused_imports)]
use log::Event::*;
use rand::{self, Rng};
use std::cell::Cell;
use std::sync::{Arc, Condvar, Mutex, Once, ONCE_INIT};
use std::thread;
use std::collections::VecDeque;
use std::mem;
use unwind;
use util::leak;
use num_cpus;

/// ////////////////////////////////////////////////////////////////////////

pub struct Registry {
    thread_infos: Vec<ThreadInfo>,
    state: Mutex<RegistryState>,
    work_available: Condvar,
}

struct RegistryState {
    terminate: bool,
    threads_at_work: usize,
    injected_jobs: VecDeque<JobRef>,
}

/// ////////////////////////////////////////////////////////////////////////
/// Initialization

static mut THE_REGISTRY: Option<&'static Registry> = None;
static THE_REGISTRY_SET: Once = ONCE_INIT;

/// Starts the worker threads (if that has not already happened). If
/// initialization has not already occurred, use the default
/// configuration.
pub fn get_registry() -> &'static Registry {
    THE_REGISTRY_SET.call_once(|| unsafe { init_registry(Configuration::new()) });
    unsafe { THE_REGISTRY.unwrap() }
}

/// Starts the worker threads (if that has not already happened) with
/// the given configuration.
pub fn get_registry_with_config(config: Configuration) -> &'static Registry {
    THE_REGISTRY_SET.call_once(|| unsafe { init_registry(config) });
    unsafe { THE_REGISTRY.unwrap() }
}

/// Initializes the global registry with the given configuration.
/// Meant to be called from within the `THE_REGISTRY_SET` once
/// function. Declared `unsafe` because it writes to `THE_REGISTRY` in
/// an unsynchronized fashion.
unsafe fn init_registry(config: Configuration) {
    let registry = leak(Registry::new(config.num_threads()));
    THE_REGISTRY = Some(registry);
}

enum Work {
    None,
    Job(JobRef),
    Terminate,
}

impl Registry {
    pub fn new(num_threads: Option<usize>) -> Arc<Registry> {
        let limit_value = match num_threads {
            Some(value) => value,
            None => num_cpus::get(),
        };

        let (workers, stealers): (Vec<_>, Vec<_>) = (0..limit_value).map(|_| deque::new()).unzip();

        let registry = Arc::new(Registry {
            thread_infos: stealers.into_iter()
                .map(|s| ThreadInfo::new(s))
                .collect(),
            state: Mutex::new(RegistryState::new()),
            work_available: Condvar::new(),
        });

        for (index, worker) in workers.into_iter().enumerate() {
            let registry = registry.clone();
            thread::spawn(move || unsafe { main_loop(worker, registry, index) });
        }

        registry
    }

    /// Returns an opaque identifier for this registry.
    pub fn id(&self) -> RegistryId {
        // We can rely on `self` not to change since we only ever create
        // registries that are boxed up in an `Arc` (see `new()` above).
        RegistryId { addr: self as *const Self as usize }
    }

    pub fn num_threads(&self) -> usize {
        self.thread_infos.len()
    }

    /// Waits for the worker threads to get up and running.  This is
    /// meant to be used for benchmarking purposes, primarily, so that
    /// you can get more consistent numbers by having everything
    /// "ready to go".
    pub fn wait_until_primed(&self) {
        for info in &self.thread_infos {
            info.primed.wait();
        }
    }

    /// ////////////////////////////////////////////////////////////////////////
    /// MAIN LOOP
    ///
    /// So long as all of the worker threads are hanging out in their
    /// top-level loop, there is no work to be done.

    fn start_working(&self, index: usize) {
        log!(StartWorking { index: index });
        {
            let mut state = self.state.lock().unwrap();
            state.threads_at_work += 1;
        }
        self.work_available.notify_all();
    }

    pub unsafe fn inject(&self, injected_jobs: &[JobRef]) {
        log!(InjectJobs { count: injected_jobs.len() });
        {
            let mut state = self.state.lock().unwrap();

            // It should not be possible for `state.terminate` to be true
            // here. It is only set to true when the user creates (and
            // drops) a `ThreadPool`; and, in that case, they cannot be
            // calling `inject()` later, since they dropped their
            // `ThreadPool`.
            assert!(!state.terminate, "inject() sees state.terminate as true");

            state.injected_jobs.extend(injected_jobs);
        }
        self.work_available.notify_all();
    }

    fn wait_for_work(&self, _worker: usize, was_active: bool) -> Work {
        log!(WaitForWork {
            worker: _worker,
            was_active: was_active,
        });

        let mut state = self.state.lock().unwrap();

        if was_active {
            state.threads_at_work -= 1;
        }

        loop {
            // Check if we need to terminate.
            if state.terminate {
                return Work::Terminate;
            }

            // Otherwise, if anything was injected from outside,
            // return that.  Note that this gives preference to
            // injected items over stealing from others, which is a
            // bit dubious, but then so is the opposite.
            if let Some(job) = state.injected_jobs.pop_front() {
                state.threads_at_work += 1;
                self.work_available.notify_all();
                return Work::Job(job);
            }

            // If any of the threads are running a job, we should spin
            // up, since they may generate subworkitems.
            if state.threads_at_work > 0 {
                return Work::None;
            }

            state = self.work_available.wait(state).unwrap();
        }
    }

    pub fn terminate(&self) {
        {
            let mut state = self.state.lock().unwrap();
            state.terminate = true;
            for job in state.injected_jobs.drain(..) {
                unsafe {
                    job.execute(JobMode::Abort);
                }
            }
        }
        self.work_available.notify_all();
    }
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct RegistryId {
    addr: usize
}

impl RegistryState {
    pub fn new() -> RegistryState {
        RegistryState {
            threads_at_work: 0,
            injected_jobs: VecDeque::new(),
            terminate: false,
        }
    }
}

struct ThreadInfo {
    // latch is set once thread has started and we are entering into
    // the main loop
    primed: LockLatch,
    stealer: Stealer<JobRef>,
}

impl ThreadInfo {
    fn new(stealer: Stealer<JobRef>) -> ThreadInfo {
        ThreadInfo {
            primed: LockLatch::new(),
            stealer: stealer,
        }
    }
}

/// ////////////////////////////////////////////////////////////////////////
/// WorkerThread identifiers

pub struct WorkerThread {
    worker: Worker<JobRef>,
    stealers: Vec<Stealer<JobRef>>,
    index: usize,

    /// A counter tracking how many calls to `Scope::spawn` occurred
    /// on the current thread; this is used by the scope code to
    /// ensure that the depth of the local deque is maintained.
    ///
    /// The actual logic here is a bit subtle. Perhaps more subtle
    /// than it has to be. The problem is this: if you have only join,
    /// then you can easily pair each push onto the deque with a pop.
    /// But when you have spawn, you push onto the deque without a
    /// corresponding pop. The `spawn_count` is used to track how many
    /// of these "unpaired pushes" have occurred.
    ///
    /// The basic pattern is that people record the spawned count
    /// before they execute a task (let's call it N). Then, if they
    /// want to pop the local tasks that this task may have spawned,
    /// they invoke `pop_spawned_jobs` with N. `pop_spawned_jobs` will
    /// pop things from the local deque and execute them until the
    /// spawn count drops to N, or the deque is empty, whichever
    /// happens first. (Either way, it resets the spawn count to N.)
    ///
    /// So e.g. join will push the right task, record the spawn count
    /// as N, run the left task, and then pop spawned jobs. Once pop
    /// spawned jobs returns, we can go ahead and try to pop the right
    /// task -- it has either been stolen, or should be on the top of the deque.
    ///
    /// Similarly, `scope` will record the spawn count and run the
    /// main task.  It can then pop the spawned jobs. At this point,
    /// until the "all done!" latch is set, it can go and steal from
    /// other people, confident in the knowledge that the local deque
    /// is empty. This is a bit subtle: basically, since all the
    /// locally spawned tasks were popped, the only way that we are
    /// not all done is if one was stolen. If one was stolen, the
    /// stuff pushed before the scope was stolen too.
    ///
    /// Finally, we have to make sure to pop spawned tasks after we
    /// steal, so as to maintain the invariant that our local deque is
    /// empty when we go to steal.
    spawn_count: Cell<usize>,

    /// A weak random number generator.
    rng: rand::XorShiftRng,

    registry: Arc<Registry>,
}

// This is a bit sketchy, but basically: the WorkerThread is
// allocated on the stack of the worker on entry and stored into this
// thread local variable. So it will remain valid at least until the
// worker is fully unwound. Using an unsafe pointer avoids the need
// for a RefCell<T> etc.
thread_local! {
    static WORKER_THREAD_STATE: Cell<*mut WorkerThread> =
        Cell::new(0 as *mut WorkerThread)
}

impl WorkerThread {
    /// Gets the `WorkerThread` index for the current thread; returns
    /// NULL if this is not a worker thread. This pointer is valid
    /// anywhere on the current thread.
    #[inline]
    pub unsafe fn current() -> *mut WorkerThread {
        WORKER_THREAD_STATE.with(|t| t.get())
    }

    /// Sets `self` as the worker thread index for the current thread.
    /// This is done during worker thread startup.
    unsafe fn set_current(&mut self) {
        WORKER_THREAD_STATE.with(|t| {
            assert!(t.get().is_null());
            t.set(self);
        });
    }

    /// Returns the registry that owns this worker thread.
    pub fn registry(&self) -> &Arc<Registry> {
        &self.registry
    }

    /// Our index amongst the worker threads (ranges from `0..self.num_threads()`).
    #[inline]
    pub fn index(&self) -> usize {
        self.index
    }

    /// Read current value of the spawn counter.
    ///
    /// See the `spawn_count` field for an extensive comment on the
    /// meaning of the spawn counter.
    #[inline]
    pub fn current_spawn_count(&self) -> usize {
        self.spawn_count.get()
    }

    /// Increment the spawn count by 1.
    ///
    /// See the `spawn_count` field for an extensive comment on the
    /// meaning of the spawn counter.
    #[inline]
    pub fn bump_spawn_count(&self) {
        self.spawn_count.set(self.spawn_count.get() + 1);
    }

    /// Pops spawned (async) jobs until our spawn count reaches
    /// `start_count` or the deque is empty. This routine is used to
    /// ensure that the local deque is "balanced".
    ///
    /// See the `spawn_count` field for an extensive comment on the
    /// meaning of the spawn counter and use of this function.
    #[inline]
    pub unsafe fn pop_spawned_jobs(&self, start_count: usize) {
        while self.spawn_count.get() != start_count {
            if let Some(job_ref) = self.pop() {
                self.spawn_count.set(self.spawn_count.get() - 1);
                job_ref.execute(JobMode::Execute);
            } else {
                self.spawn_count.set(start_count);
                break;
            }
        }
    }

    #[inline]
    pub unsafe fn push(&self, job: JobRef) {
        self.worker.push(job);
    }

    /// Pop `job` from top of stack, returning `false` if it has been
    /// stolen.
    #[inline]
    pub unsafe fn pop(&self) -> Option<JobRef> {
        self.worker.pop()
    }

    /// Keep stealing jobs until the latch is set.
    #[cold]
    pub unsafe fn steal_until(&mut self, latch: &SpinLatch) {
        let spawn_count = self.spawn_count.get();

        // If another thread stole our job when we panic, we must halt unwinding
        // until that thread is finished using it.
        let guard = unwind::finally(&latch, |latch| latch.spin());
        while !latch.probe() {
            if let Some(job) = self.steal_work() {
                debug_assert!(self.spawn_count.get() == spawn_count);
                job.execute(JobMode::Execute);
                self.pop_spawned_jobs(spawn_count);
            } else {
                thread::yield_now();
            }
        }
        mem::forget(guard);
    }

    /// Steal a single job and return it.
    unsafe fn steal_work(&mut self) -> Option<JobRef> {
        // at no point should we try to steal unless our local deque is empty
        debug_assert!(self.pop().is_none());

        if self.stealers.is_empty() {
            return None;
        }
        let start = self.rng.next_u32() % self.stealers.len() as u32;
        let (lo, hi) = self.stealers.split_at(start as usize);
        hi.iter()
            .chain(lo)
            .filter_map(|stealer| {
                match stealer.steal() {
                    Stolen::Empty => None,
                    Stolen::Abort => None, // loop?
                    Stolen::Data(v) => Some(v),
                }
            })
            .next()
    }
}

/// ////////////////////////////////////////////////////////////////////////

unsafe fn main_loop(worker: Worker<JobRef>, registry: Arc<Registry>, index: usize) {
    let stealers = registry.thread_infos
        .iter()
        .enumerate()
        .filter(|&(i, _)| i != index)
        .map(|(_, ti)| ti.stealer.clone())
        .collect::<Vec<_>>();

    assert!(stealers.len() < ::std::u32::MAX as usize,
            "We assume this is not going to happen!");

    let mut worker_thread = WorkerThread {
        worker: worker,
        stealers: stealers,
        index: index,
        spawn_count: Cell::new(0),
        rng: rand::weak_rng(),
        registry: registry.clone(),
    };
    worker_thread.set_current();

    // let registry know we are ready to do work
    registry.thread_infos[index].primed.set();

    // Worker threads should not panic. If they do, just abort, as the
    // internal state of the threadpool is corrupted. Note that if
    // **user code** panics, we should catch that and redirect.
    let abort_guard = unwind::AbortIfPanic;

    let mut was_active = false;
    loop {
        match registry.wait_for_work(index, was_active) {
            Work::Job(injected_job) => {
                injected_job.execute(JobMode::Execute);
                was_active = true;
                continue;
            }
            Work::Terminate => break,
            Work::None => {}
        }

        if let Some(stolen_job) = worker_thread.steal_work() {
            log!(StoleWork { worker: index });
            registry.start_working(index);
            debug_assert!(worker_thread.spawn_count.get() == 0);
            stolen_job.execute(JobMode::Execute);
            worker_thread.pop_spawned_jobs(0);
            was_active = true;
        } else {
            was_active = false;
        }
    }

    // Normal termination, do not abort.
    mem::forget(abort_guard);
}
