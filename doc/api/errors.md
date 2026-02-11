[English](errors.md) | [한국어](errors.ko.md)

# Error Handling & Version

Functions for retrieving error information and querying the library version at
runtime. Error codes follow the POSIX `errno` convention; zlink extends the
set with its own codes based on `ZLINK_HAUSNUMERO`.

## Error Code Constants

zlink uses a high base value to avoid collisions with system-defined `errno`
codes:

```c
#define ZLINK_HAUSNUMERO 156384712
```

### POSIX codes provided on Windows

On platforms that do not natively define certain POSIX error codes (notably
Windows), zlink defines them relative to `ZLINK_HAUSNUMERO`. On POSIX systems
the standard values are used directly.

| Constant | Value | Meaning |
|----------|-------|---------|
| `ENOTSUP` | ZLINK_HAUSNUMERO + 1 | Operation not supported |
| `EPROTONOSUPPORT` | ZLINK_HAUSNUMERO + 2 | Protocol not supported |
| `ENOBUFS` | ZLINK_HAUSNUMERO + 3 | No buffer space available |
| `ENETDOWN` | ZLINK_HAUSNUMERO + 4 | Network is down |
| `EADDRINUSE` | ZLINK_HAUSNUMERO + 5 | Address already in use |
| `EADDRNOTAVAIL` | ZLINK_HAUSNUMERO + 6 | Address not available |
| `ECONNREFUSED` | ZLINK_HAUSNUMERO + 7 | Connection refused |
| `EINPROGRESS` | ZLINK_HAUSNUMERO + 8 | Operation in progress |
| `ENOTSOCK` | ZLINK_HAUSNUMERO + 9 | Not a socket |
| `EMSGSIZE` | ZLINK_HAUSNUMERO + 10 | Message too long |
| `EAFNOSUPPORT` | ZLINK_HAUSNUMERO + 11 | Address family not supported |
| `ENETUNREACH` | ZLINK_HAUSNUMERO + 12 | Network unreachable |
| `ECONNABORTED` | ZLINK_HAUSNUMERO + 13 | Connection aborted |
| `ECONNRESET` | ZLINK_HAUSNUMERO + 14 | Connection reset |
| `ENOTCONN` | ZLINK_HAUSNUMERO + 15 | Not connected |
| `ETIMEDOUT` | ZLINK_HAUSNUMERO + 16 | Connection timed out |
| `EHOSTUNREACH` | ZLINK_HAUSNUMERO + 17 | Host unreachable |
| `ENETRESET` | ZLINK_HAUSNUMERO + 18 | Network reset |

### zlink-specific error codes

These codes are always defined and never overlap with POSIX values:

| Constant | Value | Meaning |
|----------|-------|---------|
| `EFSM` | ZLINK_HAUSNUMERO + 51 | Operation cannot be accomplished in current state (finite-state-machine error) |
| `ENOCOMPATPROTO` | ZLINK_HAUSNUMERO + 52 | No compatible protocol |
| `ETERM` | ZLINK_HAUSNUMERO + 53 | Context was terminated |
| `EMTHREAD` | ZLINK_HAUSNUMERO + 54 | No thread available |

## Version Macros

Compile-time version detection is available through the following macros
defined in `<zlink.h>`:

```c
#define ZLINK_VERSION_MAJOR 1
#define ZLINK_VERSION_MINOR 0
#define ZLINK_VERSION_PATCH 0

#define ZLINK_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))

#define ZLINK_VERSION \
    ZLINK_MAKE_VERSION(ZLINK_VERSION_MAJOR, ZLINK_VERSION_MINOR, ZLINK_VERSION_PATCH)
```

Use `ZLINK_VERSION` and `ZLINK_MAKE_VERSION` for compile-time version guards:

```c
#if ZLINK_VERSION >= ZLINK_MAKE_VERSION(1, 1, 0)
    /* use features introduced in 1.1.0 */
#endif
```

## Functions

### zlink_errno

Return the errno value for the calling thread.

```c
int zlink_errno(void);
```

Each thread maintains its own error number. After any zlink function returns a
failure indicator (typically `-1` or `NULL`), call `zlink_errno()` to obtain
the specific error code. The value is either a standard POSIX errno or one of
the `ZLINK_HAUSNUMERO`-based extended codes listed above.

**Returns:** The current thread-local errno value.

**Thread safety:** Safe to call from any thread. Each thread has an independent
error number.

**See also:** `zlink_strerror`

---

### zlink_strerror

Return a human-readable string describing the given error number.

```c
const char *zlink_strerror(int errnum_);
```

Translates both standard POSIX error codes and zlink-specific codes (such as
`EFSM`, `ETERM`, etc.) into descriptive English strings. The returned pointer
refers to static storage and must not be modified or freed.

**Returns:** A pointer to a static, null-terminated string.

**Thread safety:** Safe to call from any thread. The returned string is
statically allocated.

**See also:** `zlink_errno`

---

### zlink_version

Query the runtime library version.

```c
void zlink_version(int *major_, int *minor_, int *patch_);
```

Writes the major, minor, and patch components of the linked library version
into the provided output pointers. This allows applications to verify at
runtime that the loaded library is compatible with the headers used at compile
time.

**Returns:** None (output is written through the pointer parameters).

**Thread safety:** Safe to call from any thread at any time.

**See also:** `ZLINK_VERSION_MAJOR`, `ZLINK_VERSION_MINOR`, `ZLINK_VERSION_PATCH`, `ZLINK_MAKE_VERSION`
