# ZMQ_FD Support for ASIO - Latency Fix

**Date:** 2026-01-16
**Issue:** ROUTER_ROUTER_POLL latency 600x slower (10.5ms vs 17us)
**Status:** Root cause identified, solution designed

---

## Problem Summary

**Benchmark Results:**
| Pattern | libzmq Latency | zlink Latency | Ratio |
|---------|---------------|---------------|-------|
| Standard ROUTER_ROUTER | 17.84 us | 17.25 us | 1.0x ✅ Normal |
| **POLL ROUTER_ROUTER** | **17.98 us** | **10,512 us** | **600x** ⚠️ Critical |

**Root Cause:** ASIO doesn't support `ZMQ_FD`, causing `zmq_poll()` to limit timeout to 10ms maximum.

---

## Technical Analysis

### The ZMQ_FD Mechanism

`ZMQ_FD` is a special socket option that returns a file descriptor for the socket's mailbox. This FD can be used with system-level polling functions (select/poll/epoll) to detect when the socket has pending events.

**Normal libzmq flow:**
```c
// Get mailbox FD
fd_t mailbox_fd;
size_t fd_size = sizeof(mailbox_fd);
zmq_getsockopt(socket, ZMQ_FD, &mailbox_fd, &fd_size);  // Returns valid FD

// Use in zmq_poll()
zmq_poll(items, 1, timeout=-1);  // Can use infinite timeout
```

**zlink ASIO flow (broken):**
```c
// Try to get mailbox FD
fd_t mailbox_fd;
size_t fd_size = sizeof(mailbox_fd);
zmq_getsockopt(socket, ZMQ_FD, &mailbox_fd, &fd_size);  // Returns EINVAL ❌

// zmq_poll() workaround
zmq_poll(items, 1, timeout=-1);  // Forced to 10ms maximum! ❌
```

### Code Path Analysis

**1. src/zmq.cpp:479-483** - zmq_poll() tries to get FD:
```cpp
fd_t fd;
size_t fd_size = sizeof (fd);
if (zmq_getsockopt (socket, ZMQ_FD, &fd, &fd_size) == -1) {
    socket_fd_unavailable = true;  // ← Sets this flag!
}
```

**2. src/zmq.cpp:512-523** - Timeout limitation:
```cpp
if (socket_fd_unavailable) {
    static const int max_poll_timeout_ms = 10;  // ← 10ms limit
    if (timeout_ == 0) {
        poll_timeout = 0;
    } else if (timeout_ < 0) {
        poll_timeout = max_poll_timeout_ms;  // ← timeout=-1 becomes 10ms!
    } else {
        poll_timeout = timeout > max_poll_timeout_ms
                         ? max_poll_timeout_ms
                         : timeout;
    }
}
```

**3. src/socket_base.cpp:339-343** - ASIO returns EINVAL:
```cpp
if (option_ == ZMQ_FD) {
    //  ASIO integration does not expose mailbox file descriptors.
    errno = EINVAL;
    return -1;  // ← This is the problem!
}
```

### Impact on Latency

**Benchmark pattern** (`bench_zlink_router_router_poll.cpp:103-120`):
```cpp
for (int i = 0; i < 1000; ++i) {
    // Send message
    bench_send(router2, "ROUTER1", 7, ZMQ_SNDMORE, "lat send id");
    bench_send(router2, buffer.data(), msg_size, 0, "lat send data");

    // Wait for response - expects infinite wait, gets 10ms max!
    if (!wait_for_input(poll_r1, timeout=-1))  // ← 10ms cap!
        return;

    // Receive and reply
    bench_recv(router1, id, 256, 0, "lat recv id");
    bench_recv(router1, recv_buf.data(), msg_size, 0, "lat recv data");

    bench_send(router1, id, id_len, ZMQ_SNDMORE, "lat send id back");
    bench_send(router1, buffer.data(), msg_size, 0, "lat send data back");

    // Another poll - 10ms cap again!
    if (!wait_for_input(poll_r2, timeout=-1))  // ← 10ms cap!
        return;

    bench_recv(router2, id, 256, 0, "lat recv id back");
    bench_recv(router2, recv_buf.data(), msg_size, 0, "lat recv data back");
}

// Latency = total_time / (1000 * 2)
// 2 polls per iteration × 10ms = 20ms overhead per roundtrip
```

**Result:** Even though data is available immediately, each `zmq_poll()` waits up to 10ms before rechecking.

---

## Solution Design

### Architecture Overview

**libzmq mailbox types:**
1. **mailbox_t** - Uses ASIO + condition variables (no FD exposed)
2. **mailbox_safe_t** - Uses ASIO + condition variables + **signalers** (FD exposed!)

**Key difference:** `mailbox_safe_t` has `signaler_t` support:
```cpp
// mailbox_safe.hpp:73
std::vector<zmq::signaler_t *> _signalers;

// mailbox_safe.cpp:63-67
for (std::vector<signaler_t *>::iterator it = _signalers.begin (),
                                         end = _signalers.end ();
     it != end; ++it) {
    (*it)->send ();  // Signal all registered signalers!
}
```

**signaler_t mechanism:**
- Uses socketpair or eventfd to create a pollable file descriptor
- `get_fd()` returns `_r` (read end of the pair)
- `send()` writes to `_w` (write end), making `_r` readable
- Can be polled by system-level functions (poll/epoll/select)

### Implementation Plan

**Step 1: Add signaler support to mailbox_t**

Modify `src/mailbox.hpp`:
```cpp
class mailbox_t ZMQ_FINAL : public i_mailbox
{
  public:
    // ... existing methods ...

    // Add signaler methods like mailbox_safe_t
    void add_signaler (signaler_t *signaler_);
    void remove_signaler (signaler_t *signaler_);
    void clear_signalers ();

  private:
    // ... existing members ...

    // Add signaler vector
    std::vector<zmq::signaler_t *> _signalers;
};
```

Modify `src/mailbox.cpp`:
```cpp
void zmq::mailbox_t::send (const command_t &cmd_)
{
    _sync.lock ();
    _cpipe.write (cmd_, false);
    _cpipe.flush ();
    _cond_var.broadcast ();

    // NEW: Signal all registered signalers
    for (std::vector<signaler_t *>::iterator it = _signalers.begin (),
                                             end = _signalers.end ();
         it != end; ++it) {
        (*it)->send ();
    }

    _sync.unlock ();

    schedule_if_needed ();
}

void zmq::mailbox_t::add_signaler (signaler_t *signaler_)
{
    _signalers.push_back (signaler_);
}

void zmq::mailbox_t::remove_signaler (signaler_t *signaler_)
{
    const std::vector<zmq::signaler_t *>::iterator end = _signalers.end ();
    const std::vector<signaler_t *>::iterator it =
      std::find (_signalers.begin (), end, signaler_);
    if (it != end)
        _signalers.erase (it);
}

void zmq::mailbox_t::clear_signalers ()
{
    _signalers.clear ();
}
```

**Step 2: Expose ZMQ_FD from mailbox**

The signaler is already added via `socket_base_t::add_signaler()` for thread-safe sockets. We need to ensure it's also added for non-thread-safe sockets.

Check `src/socket_base.cpp:382-388`:
```cpp
void zmq::socket_base_t::add_signaler (signaler_t *s_)
{
    zmq_assert (_thread_safe);
    static_cast<mailbox_safe_t *> (_mailbox)->add_signaler (s_);
}
```

This only works for thread-safe sockets! We need to modify it:
```cpp
void zmq::socket_base_t::add_signaler (signaler_t *s_)
{
    if (_thread_safe) {
        static_cast<mailbox_safe_t *> (_mailbox)->add_signaler (s_);
    } else {
        static_cast<mailbox_t *> (_mailbox)->add_signaler (s_);
    }
}
```

Similarly for `remove_signaler()`.

**Step 3: Return signaler FD for ZMQ_FD**

But wait! Who owns the signaler? Looking at the code, signalers are created and managed by the socket itself when needed. For ZMQ_FD to work, we need a dedicated signaler for each socket.

Let me check how libzmq does this...

Actually, looking at `mailbox_safe_t`, the signalers are added externally, not owned by the mailbox. The socket should create and manage its own signaler for ZMQ_FD purposes.

**Better approach:** Create a dedicated signaler for ZMQ_FD in socket_base_t

Modify `src/socket_base.hpp`:
```cpp
class socket_base_t : ...
{
  private:
    // ... existing members ...

    //  Signaler for ZMQ_FD exposure (created on-demand)
    signaler_t *_zmq_fd_signaler;
};
```

Modify `src/socket_base.cpp`:
```cpp
// In constructor
zmq::socket_base_t::socket_base_t (...) :
    ...
    _zmq_fd_signaler (NULL)
{
    // ... existing code ...
}

// In destructor
zmq::socket_base_t::~socket_base_t ()
{
    // ... existing cleanup ...

    if (_zmq_fd_signaler) {
        if (_thread_safe)
            static_cast<mailbox_safe_t *> (_mailbox)->remove_signaler (_zmq_fd_signaler);
        else
            static_cast<mailbox_t *> (_mailbox)->remove_signaler (_zmq_fd_signaler);
        delete _zmq_fd_signaler;
        _zmq_fd_signaler = NULL;
    }
}

// Modify getsockopt for ZMQ_FD
if (option_ == ZMQ_FD) {
    // Create signaler on first access
    if (!_zmq_fd_signaler) {
        _zmq_fd_signaler = new (std::nothrow) signaler_t ();
        zmq_assert (_zmq_fd_signaler);

        if (_thread_safe)
            static_cast<mailbox_safe_t *> (_mailbox)->add_signaler (_zmq_fd_signaler);
        else
            static_cast<mailbox_t *> (_mailbox)->add_signaler (_zmq_fd_signaler);
    }

    return do_getsockopt<fd_t> (optval_, optvallen_,
                                _zmq_fd_signaler->get_fd ());
}
```

---

## Implementation Steps

### Phase 1: Add signaler support to mailbox_t (1 hour)

**Files to modify:**
1. `src/mailbox.hpp` - Add signaler methods and member
2. `src/mailbox.cpp` - Implement signaler methods and signal on send()

**Verification:**
- Code compiles
- Existing tests pass

### Phase 2: Add ZMQ_FD support to socket_base_t (1 hour)

**Files to modify:**
1. `src/socket_base.hpp` - Add `_zmq_fd_signaler` member
2. `src/socket_base.cpp` - Create signaler on-demand, return FD for ZMQ_FD

**Verification:**
- Code compiles
- Existing tests pass
- Manual test: `zmq_getsockopt(socket, ZMQ_FD, ...)` returns valid FD (not EINVAL)

### Phase 3: Test and verify latency fix (30 min)

**Test commands:**
```bash
# Build
./build-scripts/linux/build.sh x64 ON

# Run POLL benchmarks
cd benchwithzmq/zlink
./bench_zlink_router_router_poll tcp 64
./bench_zlink_router_router_poll ipc 64

# Compare with standard pattern
./bench_zlink_router_router tcp 64
./bench_zlink_router_router ipc 64
```

**Success criteria:**
- POLL latency: 10,512 us → ~18 us (600x improvement)
- POLL latency matches standard pattern latency
- All 61 tests passing

---

## Expected Results

### Before Fix
```
ROUTER_ROUTER_POLL (zlink):
  TCP Latency: 10,512.45 us (10.5ms)
  IPC Latency: 9,522.23 us (9.5ms)

ROUTER_ROUTER standard (zlink):
  TCP Latency: 17.25 us
  IPC Latency: 16.50 us

Gap: 600x slower!
```

### After Fix
```
ROUTER_ROUTER_POLL (zlink):
  TCP Latency: ~18 us
  IPC Latency: ~17 us

ROUTER_ROUTER standard (zlink):
  TCP Latency: 17.25 us
  IPC Latency: 16.50 us

Gap: Eliminated! ✅
```

---

## Risk Assessment

### Low Risk

**Why this is safe:**
1. **No breaking changes:** Only adds functionality, doesn't change existing behavior
2. **Lazy initialization:** Signaler only created when ZMQ_FD is accessed
3. **Proven pattern:** Uses exact same mechanism as `mailbox_safe_t` (already in production)
4. **Backward compatible:** Existing code that doesn't use ZMQ_FD is unaffected

**Test coverage:**
- All existing tests continue to pass
- `test_transport_matrix` covers POLL-based patterns
- Latency benchmarks verify performance

---

## Related Issues

### Why didn't this affect libzmq?

libzmq uses a different mailbox implementation:
- libzmq: Always uses signaler-based mailboxes with FD exposure
- zlink ASIO: Uses condition variables + ASIO, no FD by default

### Why does standard ROUTER_ROUTER work fine?

Standard pattern uses blocking `zmq_recv()`, which doesn't need ZMQ_FD:
- `zmq_recv()` internally calls `socket_base_t::recv()`
- This waits on the mailbox condition variable directly
- No need for external polling via ZMQ_FD

POLL pattern uses `zmq_poll()`:
- Needs ZMQ_FD to know when to wake up
- Without FD, falls back to 10ms timeout polling

---

## References

- **Root Cause:** `src/zmq.cpp:512-523` - 10ms timeout limit when `socket_fd_unavailable`
- **EINVAL Source:** `src/socket_base.cpp:339-343` - ASIO returns EINVAL for ZMQ_FD
- **Signaler Mechanism:** `src/signaler.hpp`, `src/signaler.cpp`
- **Working Example:** `src/mailbox_safe.cpp:63-67` - Signaler usage in mailbox_safe_t
- **Benchmark Code:** `benchwithzmq/zlink/bench_zlink_router_router_poll.cpp:103-120`

---

**Status:** Ready for implementation
**Estimated Effort:** 2.5 hours
**Expected Impact:** 600x latency improvement for POLL patterns
