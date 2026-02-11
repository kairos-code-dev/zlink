[English](utilities.md) | [한국어](utilities.ko.md)

# Utilities

Helper functions for atomic counters, scheduling timers, high-resolution
timing, thread management, and miscellaneous operations. These utilities
complement the core messaging API and are useful for building event loops,
benchmarking, and managing background threads.

## Callback Types

```c
typedef void (zlink_timer_fn)(int timer_id, void *arg);
typedef void (zlink_thread_fn)(void *);
```

`zlink_timer_fn` is the callback signature for timer expiry notifications.
The `timer_id` identifies which timer fired and `arg` is the user-provided
context pointer passed when the timer was created.

`zlink_thread_fn` is the entry-point signature for threads started with
`zlink_threadstart`.

## Atomic Counter

Atomic counters provide lock-free increment, decrement, and read operations on
a shared integer. The counter is created with `zlink_atomic_counter_new` and
must be destroyed with `zlink_atomic_counter_destroy`.

> **Note:** Only `zlink_atomic_counter_new` is exported from the shared
> library (`ZLINK_EXPORT`). The remaining five functions are declared without
> the export attribute but are still public API and available when linking
> statically or through the header.

### zlink_atomic_counter_new

Create a new atomic counter initialized to zero.

```c
void *zlink_atomic_counter_new(void);
```

Allocates and returns an opaque handle to an atomic counter with an initial
value of zero.

**Returns:** Counter handle on success, or `NULL` on failure (out of memory).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_atomic_counter_set`, `zlink_atomic_counter_destroy`

---

### zlink_atomic_counter_set

Set the counter to an explicit value.

```c
void zlink_atomic_counter_set(void *counter_, int value_);
```

Atomically replaces the current counter value with `value_`.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_atomic_counter_value`

---

### zlink_atomic_counter_inc

Increment the counter by one.

```c
int zlink_atomic_counter_inc(void *counter_);
```

Atomically increments the counter and returns the previous value (the value
immediately before the increment).

**Returns:** The value of the counter before the increment.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_atomic_counter_dec`

---

### zlink_atomic_counter_dec

Decrement the counter by one.

```c
int zlink_atomic_counter_dec(void *counter_);
```

Atomically decrements the counter and returns the previous value (the value
immediately before the decrement).

**Returns:** The value of the counter before the decrement.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_atomic_counter_inc`

---

### zlink_atomic_counter_value

Return the current counter value.

```c
int zlink_atomic_counter_value(void *counter_);
```

Reads the current value of the counter atomically.

**Returns:** The current counter value.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_atomic_counter_set`

---

### zlink_atomic_counter_destroy

Destroy the counter and release its memory.

```c
void zlink_atomic_counter_destroy(void **counter_p_);
```

Releases the counter handle. The pointer at `*counter_p_` is set to `NULL`
after destruction.

**Thread safety:** Must not be called while other threads are operating on the
same counter.

**See also:** `zlink_atomic_counter_new`

---

## Timers

Scheduling timers allow you to register callbacks that fire after a specified
interval. Timers are managed as a set: create a set with `zlink_timers_new`,
add individual timers with `zlink_timers_add`, and drive execution by calling
`zlink_timers_execute` from your event loop.

### zlink_timers_new

Create a new timer set.

```c
void *zlink_timers_new(void);
```

Allocates and returns an opaque handle to an empty timer set.

**Returns:** Timer-set handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_timers_destroy`, `zlink_timers_add`

---

### zlink_timers_destroy

Destroy a timer set and release all resources.

```c
int zlink_timers_destroy(void **timers_p);
```

Cancels all timers in the set and frees the handle. The pointer at
`*timers_p` is set to `NULL` after destruction.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called while other threads are operating on the
same timer set.

**See also:** `zlink_timers_new`

---

### zlink_timers_add

Add a timer with the given interval and callback.

```c
int zlink_timers_add(void *timers, size_t interval, zlink_timer_fn handler, void *arg);
```

Registers a new timer that fires after `interval` milliseconds. When the timer
expires, `handler` is called with the timer's ID and the user-provided `arg`.
The timer repeats automatically at the same interval until cancelled.

**Returns:** A non-negative timer ID on success, or `-1` on failure (errno is
set).

**Thread safety:** Must not be called concurrently with other operations on
the same timer set.

**See also:** `zlink_timers_cancel`, `zlink_timers_set_interval`

---

### zlink_timers_cancel

Cancel a timer by its ID.

```c
int zlink_timers_cancel(void *timers, int timer_id);
```

Removes the timer from the set. Its callback will no longer be invoked.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same timer set.

**See also:** `zlink_timers_add`

---

### zlink_timers_set_interval

Change the interval of an existing timer.

```c
int zlink_timers_set_interval(void *timers, int timer_id, size_t interval);
```

Updates the timer's interval to `interval` milliseconds. The new interval
takes effect after the current cycle completes.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same timer set.

**See also:** `zlink_timers_add`, `zlink_timers_reset`

---

### zlink_timers_reset

Reset a timer's countdown to its full interval.

```c
int zlink_timers_reset(void *timers, int timer_id);
```

Restarts the timer's countdown from the beginning of its current interval,
effectively postponing the next expiry.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same timer set.

**See also:** `zlink_timers_set_interval`

---

### zlink_timers_timeout

Return the time until the next timer fires.

```c
long zlink_timers_timeout(void *timers);
```

Computes the number of milliseconds remaining until the earliest timer in the
set expires. This value is suitable for passing directly as the `timeout_`
argument to `zlink_poll`.

**Returns:** Milliseconds until the next expiry, or `-1` if no timers are
registered.

**Thread safety:** Must not be called concurrently with other operations on
the same timer set.

**See also:** `zlink_timers_execute`, `zlink_poll`

---

### zlink_timers_execute

Execute all expired timers.

```c
int zlink_timers_execute(void *timers);
```

Checks every timer in the set and invokes the callback for each one whose
interval has elapsed. Typically called in a loop together with
`zlink_timers_timeout` and `zlink_poll`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same timer set.

**See also:** `zlink_timers_timeout`, `zlink_timers_add`

---

## Stopwatch

High-resolution timing functions for benchmarking and profiling. Start a
stopwatch, take intermediate readings, and stop it to get the total elapsed
time in microseconds.

### zlink_stopwatch_start

Start a high-resolution stopwatch.

```c
void *zlink_stopwatch_start(void);
```

Captures the current time and returns an opaque handle used to measure elapsed
time. The handle must eventually be released by `zlink_stopwatch_stop`.

**Returns:** An opaque stopwatch handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread. The returned handle should be
used by one thread at a time.

**See also:** `zlink_stopwatch_intermediate`, `zlink_stopwatch_stop`

---

### zlink_stopwatch_intermediate

Return elapsed microseconds without stopping the stopwatch.

```c
unsigned long zlink_stopwatch_intermediate(void *watch_);
```

Reads the elapsed time since `zlink_stopwatch_start` was called, without
releasing the handle. May be called multiple times to take successive
measurements.

**Returns:** Elapsed time in microseconds.

**Thread safety:** Must not be called concurrently with `zlink_stopwatch_stop`
on the same handle.

**See also:** `zlink_stopwatch_start`, `zlink_stopwatch_stop`

---

### zlink_stopwatch_stop

Stop the stopwatch and return total elapsed microseconds.

```c
unsigned long zlink_stopwatch_stop(void *watch_);
```

Returns the total elapsed time since `zlink_stopwatch_start` was called and
releases the stopwatch handle. The handle must not be used after this call.

**Returns:** Elapsed time in microseconds.

**Thread safety:** Must not be called concurrently with other operations on
the same handle.

**See also:** `zlink_stopwatch_start`, `zlink_stopwatch_intermediate`

---

## Miscellaneous

### zlink_sleep

Sleep for the given number of seconds.

```c
void zlink_sleep(int seconds_);
```

Suspends the calling thread for at least `seconds_` seconds. This is a
portable convenience wrapper around platform-specific sleep functions.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_stopwatch_start`

---

### zlink_threadstart

Start a new thread running the given function.

```c
void *zlink_threadstart(zlink_thread_fn *func_, void *arg_);
```

Creates and starts a new operating-system thread that executes `func_` with
`arg_` as its sole argument. The returned handle must be passed to
`zlink_threadclose` to wait for completion and release resources.

**Returns:** An opaque thread handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_threadclose`

---

### zlink_threadclose

Wait for a thread to finish and release its handle.

```c
void zlink_threadclose(void *thread_);
```

Blocks the calling thread until the thread identified by `thread_` has
terminated, then releases the handle. The handle must not be used after this
call.

**Thread safety:** Must be called exactly once per handle. Do not call from
the thread being joined.

**See also:** `zlink_threadstart`
