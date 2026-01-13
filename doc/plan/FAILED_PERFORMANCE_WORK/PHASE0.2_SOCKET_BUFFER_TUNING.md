# Phase 0.2: Socket Buffer Tuning Implementation

## Overview
Implementation of TCP socket buffer tuning to improve throughput for large data transfers.

## Changes Made

### 1. Default Buffer Size Change (`src/options.cpp`)
**File**: `/home/ulalax/project/ulalax/zlink/src/options.cpp`

Changed default values from -1 (OS default) to 512KB (524288 bytes):

```cpp
// Before:
sndbuf (-1),
rcvbuf (-1),

// After:
sndbuf (524288),  // 512KB default for better throughput
rcvbuf (524288),  // 512KB default for better throughput
```

**Line numbers**: 148-149

### 2. Debug Logging (`src/tcp.cpp`)
**File**: `/home/ulalax/project/ulalax/zlink/src/tcp.cpp`

Added debug logging in `set_tcp_send_buffer()` and `set_tcp_receive_buffer()` functions to show requested vs actual buffer sizes:

```cpp
#ifndef NDEBUG
    // Log actual buffer size after setting (may differ from requested)
    if (rc == 0) {
        int actual_bufsize = 0;
        socklen_t optlen = sizeof (actual_bufsize);
        const int get_rc = getsockopt (sockfd_, SOL_SOCKET, SO_SNDBUF,
                                       reinterpret_cast<char *> (&actual_bufsize),
                                       &optlen);
        if (get_rc == 0) {
            fprintf (stderr, "[TCP] SO_SNDBUF: requested=%d, actual=%d\n",
                     bufsize_, actual_bufsize);
        }
    }
#endif
```

**Line numbers**:
- `set_tcp_send_buffer()`: 60-73
- `set_tcp_receive_buffer()`: 85-98

## Verification

### Test Results

All tests passed successfully:

1. **Default Buffer Size Test**: ✓ PASS
   - ZMQ_SNDBUF: 524288 bytes (512.0 KB)
   - ZMQ_RCVBUF: 524288 bytes (512.0 KB)

2. **Override Test**: ✓ PASS
   - Successfully override to 256 KB
   - Successfully override to 1 MB

3. **OS Default (-1) Test**: ✓ PASS
   - Can still set to -1 to use OS default
   - Maintains backward compatibility

4. **TCP Connection Test**: ✓ PASS
   - TCP connections work correctly with 512KB buffers
   - Message send/receive verified

### API Compatibility

The implementation maintains full API compatibility:

- Users can still override buffer sizes using `ZMQ_SNDBUF` and `ZMQ_RCVBUF` socket options
- Setting value to -1 still works (uses OS default, skips setsockopt call)
- No breaking changes to existing applications

## Debug Logging Behavior

The buffer logging is controlled by `#ifndef NDEBUG`:

- **Debug builds** (`CMAKE_BUILD_TYPE=Debug`): Logging enabled
  - Output: `[TCP] SO_SNDBUF: requested=524288, actual=<value>`
  - Output: `[TCP] SO_RCVBUF: requested=524288, actual=<value>`

- **Release builds** (`CMAKE_BUILD_TYPE=Release`, `RelWithDebInfo`): Logging disabled
  - No performance impact in production

**Note**: The actual buffer size reported by the OS may differ from the requested size due to:
- OS minimum/maximum limits
- Kernel buffer management policies
- Platform-specific behavior

## Performance Impact

### Benefits
- **Improved throughput**: 512KB buffers reduce syscall overhead for large transfers
- **Better network utilization**: Larger buffers allow for more efficient TCP window scaling
- **Reduced latency variance**: More buffering helps smooth out network jitter

### Considerations
- **Memory usage**: Each TCP socket now uses ~1MB total (512KB send + 512KB receive)
- **Tunable**: Applications can still override to smaller values if needed
- **OS limits**: Some systems may cap the actual buffer size (check `/proc/sys/net/core/rmem_max` and `wmem_max` on Linux)

## Build and Test

```bash
# Build with default settings (Release)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Build with debug logging
cmake -B build_debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build_debug

# Run tests
cd build && ctest --output-on-failure
```

## Platform-Specific Notes

### Linux
- Default OS max: `/proc/sys/net/core/rmem_max` and `wmem_max` (typically 128KB-4MB)
- Actual buffer may be doubled by kernel for bookkeeping
- Check with: `sysctl net.core.rmem_max net.core.wmem_max`

### macOS
- Default limits typically allow 512KB without issue
- Check with: `sysctl kern.ipc.maxsockbuf`

### Windows
- Generally allows larger buffers
- No special configuration needed

## Related Files

- `/home/ulalax/project/ulalax/zlink/src/options.cpp` - Default value changes
- `/home/ulalax/project/ulalax/zlink/src/tcp.cpp` - Debug logging implementation
- `/home/ulalax/project/ulalax/zlink/src/options.hpp` - Option declarations (unchanged)

## Implementation Status

✓ **COMPLETED**

- [x] Change default sndbuf from -1 to 524288
- [x] Change default rcvbuf from -1 to 524288
- [x] Add debug logging for SO_SNDBUF
- [x] Add debug logging for SO_RCVBUF
- [x] Verify backward compatibility
- [x] Test buffer override functionality
- [x] Test TCP connections with new defaults
- [x] Verify build on RelWithDebInfo and Debug modes

## Next Steps

This implementation is ready for:
1. Integration testing with benchmarks
2. Performance comparison with previous default (-1)
3. Merge to feature/perf-optimization branch
4. Documentation updates in main README if needed
