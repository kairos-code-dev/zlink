/* SPDX-License-Identifier: MPL-2.0 */
/*  *************************************************************************
    NOTE to contributors. This file comprises the principal public contract
    for Zlink API users. Any change to this file supplied in a stable
    release SHOULD not break existing applications.
    In practice this means that the value of constants must not change, and
    that old values may not be reused for new constants.
    *************************************************************************
*/

#ifndef __ZLINK_H_INCLUDED__
#define __ZLINK_H_INCLUDED__

/*  Version macros for compile-time API version detection                     */
#define ZLINK_VERSION_MAJOR 4
#define ZLINK_VERSION_MINOR 3
#define ZLINK_VERSION_PATCH 5

#define ZLINK_MAKE_VERSION(major, minor, patch)                                  \
    ((major) *10000 + (minor) *100 + (patch))
#define ZLINK_VERSION                                                            \
    ZLINK_MAKE_VERSION (ZLINK_VERSION_MAJOR, ZLINK_VERSION_MINOR, ZLINK_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

#if !defined _WIN32_WCE
#include <errno.h>
#endif
#include <stddef.h>
#include <stdio.h>

/*  Handle DSO symbol visibility                                             */
#if defined ZLINK_NO_EXPORT
#define ZLINK_EXPORT
#else
#if defined _WIN32
#if defined ZLINK_STATIC
#define ZLINK_EXPORT
#elif defined DLL_EXPORT
#define ZLINK_EXPORT __declspec(dllexport)
#else
#define ZLINK_EXPORT __declspec(dllimport)
#endif
#else
#if defined __SUNPRO_C || defined __SUNPRO_CC
#define ZLINK_EXPORT __global
#elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#define ZLINK_EXPORT __attribute__ ((visibility ("default")))
#else
#define ZLINK_EXPORT
#endif
#endif
#endif

/*  Define integer types needed for event interface                          */
#define ZLINK_DEFINED_STDINT 1
#if defined ZLINK_HAVE_SOLARIS || defined ZLINK_HAVE_OPENVMS
#include <inttypes.h>
#elif defined _MSC_VER && _MSC_VER < 1600
#ifndef uint64_t
typedef unsigned __int64 uint64_t;
#endif
#ifndef int32_t
typedef __int32 int32_t;
#endif
#ifndef uint32_t
typedef unsigned __int32 uint32_t;
#endif
#ifndef uint16_t
typedef unsigned __int16 uint16_t;
#endif
#ifndef uint8_t
typedef unsigned __int8 uint8_t;
#endif
#else
#include <stdint.h>
#endif

#if !defined _WIN32
// needed for sigset_t definition in zlink_ppoll
#include <signal.h>
#endif

//  32-bit AIX's pollfd struct members are called reqevents and rtnevents so it
//  defines compatibility macros for them. Need to include that header first to
//  stop build failures since zlink_pollset_t defines them as events and revents.
#ifdef ZLINK_HAVE_AIX
#include <poll.h>
#endif


/******************************************************************************/
/*  0MQ errors.                                                               */
/******************************************************************************/

/*  A number random enough not to collide with different errno ranges on      */
/*  different OSes. The assumption is that error_t is at least 32-bit type.   */
#define ZLINK_HAUSNUMERO 156384712

/*  On Windows platform some of the standard POSIX errnos are not defined.    */
#ifndef ENOTSUP
#define ENOTSUP (ZLINK_HAUSNUMERO + 1)
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (ZLINK_HAUSNUMERO + 2)
#endif
#ifndef ENOBUFS
#define ENOBUFS (ZLINK_HAUSNUMERO + 3)
#endif
#ifndef ENETDOWN
#define ENETDOWN (ZLINK_HAUSNUMERO + 4)
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (ZLINK_HAUSNUMERO + 5)
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (ZLINK_HAUSNUMERO + 6)
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (ZLINK_HAUSNUMERO + 7)
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (ZLINK_HAUSNUMERO + 8)
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (ZLINK_HAUSNUMERO + 9)
#endif
#ifndef EMSGSIZE
#define EMSGSIZE (ZLINK_HAUSNUMERO + 10)
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT (ZLINK_HAUSNUMERO + 11)
#endif
#ifndef ENETUNREACH
#define ENETUNREACH (ZLINK_HAUSNUMERO + 12)
#endif
#ifndef ECONNABORTED
#define ECONNABORTED (ZLINK_HAUSNUMERO + 13)
#endif
#ifndef ECONNRESET
#define ECONNRESET (ZLINK_HAUSNUMERO + 14)
#endif
#ifndef ENOTCONN
#define ENOTCONN (ZLINK_HAUSNUMERO + 15)
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT (ZLINK_HAUSNUMERO + 16)
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH (ZLINK_HAUSNUMERO + 17)
#endif
#ifndef ENETRESET
#define ENETRESET (ZLINK_HAUSNUMERO + 18)
#endif

/*  Native 0MQ error codes.                                                   */
#define EFSM (ZLINK_HAUSNUMERO + 51)
#define ENOCOMPATPROTO (ZLINK_HAUSNUMERO + 52)
#define ETERM (ZLINK_HAUSNUMERO + 53)
#define EMTHREAD (ZLINK_HAUSNUMERO + 54)

/*  This function retrieves the errno as it is known to 0MQ library. The goal */
/*  of this function is to make the code 100% portable, including where 0MQ   */
/*  compiled with certain CRT library (on Windows) is linked to an            */
/*  application that uses different CRT library.                              */
ZLINK_EXPORT int zlink_errno (void);

/*  Resolves system errors and 0MQ errors to human-readable string.           */
ZLINK_EXPORT const char *zlink_strerror (int errnum_);

/*  Run-time API version detection                                            */
ZLINK_EXPORT void zlink_version (int *major_, int *minor_, int *patch_);

/******************************************************************************/
/*  0MQ infrastructure (a.k.a. context) initialisation & termination.         */
/******************************************************************************/

/*  Context options                                                           */
#define ZLINK_IO_THREADS 1
#define ZLINK_MAX_SOCKETS 2
#define ZLINK_SOCKET_LIMIT 3
#define ZLINK_THREAD_PRIORITY 3
#define ZLINK_THREAD_SCHED_POLICY 4
#define ZLINK_MAX_MSGSZ 5
#define ZLINK_MSG_T_SIZE 6
#define ZLINK_THREAD_AFFINITY_CPU_ADD 7
#define ZLINK_THREAD_AFFINITY_CPU_REMOVE 8
#define ZLINK_THREAD_NAME_PREFIX 9

/*  Default for new contexts                                                  */
#define ZLINK_IO_THREADS_DFLT 1
#define ZLINK_MAX_SOCKETS_DFLT 1023
#define ZLINK_THREAD_PRIORITY_DFLT -1
#define ZLINK_THREAD_SCHED_POLICY_DFLT -1

ZLINK_EXPORT void *zlink_ctx_new (void);
ZLINK_EXPORT int zlink_ctx_term (void *context_);
ZLINK_EXPORT int zlink_ctx_shutdown (void *context_);
ZLINK_EXPORT int zlink_ctx_set (void *context_, int option_, int optval_);
ZLINK_EXPORT int zlink_ctx_get (void *context_, int option_);

/*  Old (legacy) API                                                          */
ZLINK_EXPORT void *zlink_init (int io_threads_);
ZLINK_EXPORT int zlink_term (void *context_);
ZLINK_EXPORT int zlink_ctx_destroy (void *context_);


/******************************************************************************/
/*  0MQ message definition.                                                   */
/******************************************************************************/

/* Some architectures, like sparc64 and some variants of aarch64, enforce pointer
 * alignment and raise sigbus on violations. Make sure applications allocate
 * zlink_msg_t on addresses aligned on a pointer-size boundary to avoid this issue.
 */
typedef struct zlink_msg_t
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    __declspec(align (8)) unsigned char _[64];
#elif defined(_MSC_VER)                                                        \
  && (defined(_M_IX86) || defined(_M_ARM_ARMV7VE) || defined(_M_ARM))
    __declspec(align (4)) unsigned char _[64];
#elif defined(__GNUC__) || defined(__INTEL_COMPILER)                           \
  || (defined(__SUNPRO_C) && __SUNPRO_C >= 0x590)                              \
  || (defined(__SUNPRO_CC) && __SUNPRO_CC >= 0x590)
    unsigned char _[64] __attribute__ ((aligned (sizeof (void *))));
#else
    unsigned char _[64];
#endif
} zlink_msg_t;

typedef void (zlink_free_fn) (void *data_, void *hint_);

ZLINK_EXPORT int zlink_msg_init (zlink_msg_t *msg_);
ZLINK_EXPORT int zlink_msg_init_size (zlink_msg_t *msg_, size_t size_);
ZLINK_EXPORT int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_);
ZLINK_EXPORT int zlink_msg_send (zlink_msg_t *msg_, void *s_, int flags_);
ZLINK_EXPORT int zlink_msg_recv (zlink_msg_t *msg_, void *s_, int flags_);
ZLINK_EXPORT int zlink_msg_close (zlink_msg_t *msg_);
ZLINK_EXPORT int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_);
ZLINK_EXPORT int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_);
ZLINK_EXPORT void *zlink_msg_data (zlink_msg_t *msg_);
ZLINK_EXPORT size_t zlink_msg_size (const zlink_msg_t *msg_);
ZLINK_EXPORT int zlink_msg_more (const zlink_msg_t *msg_);
ZLINK_EXPORT int zlink_msg_get (const zlink_msg_t *msg_, int property_);
ZLINK_EXPORT int zlink_msg_set (zlink_msg_t *msg_, int property_, int optval_);
ZLINK_EXPORT const char *zlink_msg_gets (const zlink_msg_t *msg_,
                                     const char *property_);

/******************************************************************************/
/*  0MQ socket definition.                                                    */
/******************************************************************************/

/*  Socket types.                                                             */
#define ZLINK_PAIR 0
#define ZLINK_PUB 1
#define ZLINK_SUB 2
#define ZLINK_REQ 3
#define ZLINK_REP 4
#define ZLINK_DEALER 5
#define ZLINK_ROUTER 6
#define ZLINK_PULL 7
#define ZLINK_PUSH 8
#define ZLINK_XPUB 9
#define ZLINK_XSUB 10
#define ZLINK_STREAM 11

/*  Deprecated aliases                                                        */
#define ZLINK_XREQ ZLINK_DEALER
#define ZLINK_XREP ZLINK_ROUTER

/*  Socket options.                                                           */
#define ZLINK_AFFINITY 4
#define ZLINK_ROUTING_ID 5
#define ZLINK_SUBSCRIBE 6
#define ZLINK_UNSUBSCRIBE 7
#define ZLINK_RATE 8
#define ZLINK_RECOVERY_IVL 9
#define ZLINK_SNDBUF 11
#define ZLINK_RCVBUF 12
#define ZLINK_RCVMORE 13
#define ZLINK_FD 14
#define ZLINK_EVENTS 15
#define ZLINK_TYPE 16
#define ZLINK_LINGER 17
#define ZLINK_RECONNECT_IVL 18
#define ZLINK_BACKLOG 19
#define ZLINK_RECONNECT_IVL_MAX 21
#define ZLINK_MAXMSGSIZE 22
#define ZLINK_SNDHWM 23
#define ZLINK_RCVHWM 24
#define ZLINK_MULTICAST_HOPS 25
#define ZLINK_RCVTIMEO 27
#define ZLINK_SNDTIMEO 28
#define ZLINK_LAST_ENDPOINT 32
#define ZLINK_ROUTER_MANDATORY 33
#define ZLINK_TCP_KEEPALIVE 34
#define ZLINK_TCP_KEEPALIVE_CNT 35
#define ZLINK_TCP_KEEPALIVE_IDLE 36
#define ZLINK_TCP_KEEPALIVE_INTVL 37
#define ZLINK_IMMEDIATE 39
#define ZLINK_XPUB_VERBOSE 40
#define ZLINK_ROUTER_RAW 41
#define ZLINK_IPV6 42
#define ZLINK_MECHANISM 43
#define ZLINK_PLAIN_SERVER 44
#define ZLINK_PLAIN_USERNAME 45
#define ZLINK_PLAIN_PASSWORD 46
#define ZLINK_CURVE_SERVER 47
#define ZLINK_CURVE_PUBLICKEY 48
#define ZLINK_CURVE_SECRETKEY 49
#define ZLINK_CURVE_SERVERKEY 50
#define ZLINK_PROBE_ROUTER 51
#define ZLINK_REQ_CORRELATE 52
#define ZLINK_REQ_RELAXED 53
#define ZLINK_CONFLATE 54
#define ZLINK_ZAP_DOMAIN 55
#define ZLINK_ROUTER_HANDOVER 56
#define ZLINK_TOS 57
#define ZLINK_CONNECT_ROUTING_ID 61
#define ZLINK_GSSAPI_SERVER 62
#define ZLINK_GSSAPI_PRINCIPAL 63
#define ZLINK_GSSAPI_SERVICE_PRINCIPAL 64
#define ZLINK_GSSAPI_PLAINTEXT 65
#define ZLINK_HANDSHAKE_IVL 66
#define ZLINK_XPUB_NODROP 69
#define ZLINK_BLOCKY 70
#define ZLINK_XPUB_MANUAL 71
#define ZLINK_XPUB_WELCOME_MSG 72
#define ZLINK_STREAM_NOTIFY 73
#define ZLINK_INVERT_MATCHING 74
#define ZLINK_HEARTBEAT_IVL 75
#define ZLINK_HEARTBEAT_TTL 76
#define ZLINK_HEARTBEAT_TIMEOUT 77
#define ZLINK_XPUB_VERBOSER 78
#define ZLINK_CONNECT_TIMEOUT 79
#define ZLINK_TCP_MAXRT 80
#define ZLINK_THREAD_SAFE 81
#define ZLINK_MULTICAST_MAXTPDU 84
#define ZLINK_VMCI_BUFFER_SIZE 85
#define ZLINK_VMCI_BUFFER_MIN_SIZE 86
#define ZLINK_VMCI_BUFFER_MAX_SIZE 87
#define ZLINK_VMCI_CONNECT_TIMEOUT 88
#define ZLINK_USE_FD 89
#define ZLINK_GSSAPI_PRINCIPAL_NAMETYPE 90
#define ZLINK_GSSAPI_SERVICE_PRINCIPAL_NAMETYPE 91
#define ZLINK_BINDTODEVICE 92

/*  Message options                                                           */
#define ZLINK_MORE 1
#define ZLINK_SHARED 3

/*  Send/recv options.                                                        */
#define ZLINK_DONTWAIT 1
#define ZLINK_SNDMORE 2

/*  Security mechanisms                                                       */
#define ZLINK_NULL 0
#define ZLINK_PLAIN 1
#define ZLINK_CURVE 2
#define ZLINK_GSSAPI 3

/*  RADIO-DISH protocol                                                       */
#define ZLINK_GROUP_MAX_LENGTH 255

/*  Deprecated options and aliases                                            */
#define ZLINK_IDENTITY ZLINK_ROUTING_ID
#define ZLINK_CONNECT_RID ZLINK_CONNECT_ROUTING_ID
#define ZLINK_TCP_ACCEPT_FILTER 38
#define ZLINK_IPC_FILTER_PID 58
#define ZLINK_IPC_FILTER_UID 59
#define ZLINK_IPC_FILTER_GID 60
#define ZLINK_IPV4ONLY 31
#define ZLINK_DELAY_ATTACH_ON_CONNECT ZLINK_IMMEDIATE
#define ZLINK_NOBLOCK ZLINK_DONTWAIT
#define ZLINK_FAIL_UNROUTABLE ZLINK_ROUTER_MANDATORY
#define ZLINK_ROUTER_BEHAVIOR ZLINK_ROUTER_MANDATORY

/*  Deprecated Message options                                                */
#define ZLINK_SRCFD 2

/******************************************************************************/
/*  GSSAPI definitions                                                        */
/******************************************************************************/

/*  GSSAPI principal name types                                               */
#define ZLINK_GSSAPI_NT_HOSTBASED 0
#define ZLINK_GSSAPI_NT_USER_NAME 1
#define ZLINK_GSSAPI_NT_KRB5_PRINCIPAL 2

/******************************************************************************/
/*  0MQ socket events and monitoring                                          */
/******************************************************************************/

/*  Socket transport events (TCP, IPC and TIPC only)                          */

#define ZLINK_EVENT_CONNECTED 0x0001
#define ZLINK_EVENT_CONNECT_DELAYED 0x0002
#define ZLINK_EVENT_CONNECT_RETRIED 0x0004
#define ZLINK_EVENT_LISTENING 0x0008
#define ZLINK_EVENT_BIND_FAILED 0x0010
#define ZLINK_EVENT_ACCEPTED 0x0020
#define ZLINK_EVENT_ACCEPT_FAILED 0x0040
#define ZLINK_EVENT_CLOSED 0x0080
#define ZLINK_EVENT_CLOSE_FAILED 0x0100
#define ZLINK_EVENT_DISCONNECTED 0x0200
#define ZLINK_EVENT_MONITOR_STOPPED 0x0400
#define ZLINK_EVENT_ALL 0xFFFF
/*  Unspecified system errors during handshake. Event value is an errno.      */
#define ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL 0x0800
/*  Handshake complete successfully with successful authentication (if        *
 *  enabled). Event value is unused.                                          */
#define ZLINK_EVENT_HANDSHAKE_SUCCEEDED 0x1000
/*  Protocol errors between ZMTP peers or between server and ZAP handler.     *
 *  Event value is one of ZLINK_PROTOCOL_ERROR_*                                */
#define ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL 0x2000
/*  Failed authentication requests. Event value is the numeric ZAP status     *
 *  code, i.e. 300, 400 or 500.                                               */
#define ZLINK_EVENT_HANDSHAKE_FAILED_AUTH 0x4000
#define ZLINK_PROTOCOL_ERROR_ZMTP_UNSPECIFIED 0x10000000
#define ZLINK_PROTOCOL_ERROR_ZMTP_UNEXPECTED_COMMAND 0x10000001
#define ZLINK_PROTOCOL_ERROR_ZMTP_INVALID_SEQUENCE 0x10000002
#define ZLINK_PROTOCOL_ERROR_ZMTP_KEY_EXCHANGE 0x10000003
#define ZLINK_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_UNSPECIFIED 0x10000011
#define ZLINK_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_MESSAGE 0x10000012
#define ZLINK_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_HELLO 0x10000013
#define ZLINK_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_INITIATE 0x10000014
#define ZLINK_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_ERROR 0x10000015
#define ZLINK_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_READY 0x10000016
#define ZLINK_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_WELCOME 0x10000017
#define ZLINK_PROTOCOL_ERROR_ZMTP_INVALID_METADATA 0x10000018
// the following two may be due to erroneous configuration of a peer
#define ZLINK_PROTOCOL_ERROR_ZMTP_CRYPTOGRAPHIC 0x11000001
#define ZLINK_PROTOCOL_ERROR_ZMTP_MECHANISM_MISMATCH 0x11000002
#define ZLINK_PROTOCOL_ERROR_ZAP_UNSPECIFIED 0x20000000
#define ZLINK_PROTOCOL_ERROR_ZAP_MALFORMED_REPLY 0x20000001
#define ZLINK_PROTOCOL_ERROR_ZAP_BAD_REQUEST_ID 0x20000002
#define ZLINK_PROTOCOL_ERROR_ZAP_BAD_VERSION 0x20000003
#define ZLINK_PROTOCOL_ERROR_ZAP_INVALID_STATUS_CODE 0x20000004
#define ZLINK_PROTOCOL_ERROR_ZAP_INVALID_METADATA 0x20000005
#define ZLINK_PROTOCOL_ERROR_WS_UNSPECIFIED 0x30000000

ZLINK_EXPORT void *zlink_socket (void *, int type_);
ZLINK_EXPORT int zlink_close (void *s_);
ZLINK_EXPORT int
zlink_setsockopt (void *s_, int option_, const void *optval_, size_t optvallen_);
ZLINK_EXPORT int
zlink_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_);
ZLINK_EXPORT int zlink_bind (void *s_, const char *addr_);
ZLINK_EXPORT int zlink_connect (void *s_, const char *addr_);
ZLINK_EXPORT int zlink_unbind (void *s_, const char *addr_);
ZLINK_EXPORT int zlink_disconnect (void *s_, const char *addr_);
ZLINK_EXPORT int zlink_send (void *s_, const void *buf_, size_t len_, int flags_);
ZLINK_EXPORT int
zlink_send_const (void *s_, const void *buf_, size_t len_, int flags_);
ZLINK_EXPORT int zlink_recv (void *s_, void *buf_, size_t len_, int flags_);
ZLINK_EXPORT int zlink_socket_monitor (void *s_, const char *addr_, int events_);

/******************************************************************************/
/*  Hide socket fd type; this was before zlink_poller_event_t typedef below     */
/******************************************************************************/

#if defined _WIN32
// Windows uses a pointer-sized unsigned integer to store the socket fd.
#if defined _WIN64
typedef unsigned __int64 zlink_fd_t;
#else
typedef unsigned int zlink_fd_t;
#endif
#else
typedef int zlink_fd_t;
#endif

/******************************************************************************/
/*  Deprecated I/O multiplexing. Prefer using zlink_poller API                  */
/******************************************************************************/

#define ZLINK_POLLIN 1
#define ZLINK_POLLOUT 2
#define ZLINK_POLLERR 4
#define ZLINK_POLLPRI 8

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;

#define ZLINK_POLLITEMS_DFLT 16

ZLINK_EXPORT int zlink_poll (zlink_pollitem_t *items_, int nitems_, long timeout_);

/******************************************************************************/
/*  Message proxying                                                          */
/******************************************************************************/

ZLINK_EXPORT int zlink_proxy (void *frontend_, void *backend_, void *capture_);
ZLINK_EXPORT int zlink_proxy_steerable (void *frontend_,
                                    void *backend_,
                                    void *capture_,
                                    void *control_);

/******************************************************************************/
/*  Probe library capabilities                                                */
/******************************************************************************/

#define ZLINK_HAS_CAPABILITIES 1
ZLINK_EXPORT int zlink_has (const char *capability_);

/*  Deprecated aliases */
#define ZLINK_STREAMER 1
#define ZLINK_FORWARDER 2
#define ZLINK_QUEUE 3

/*  Deprecated methods */
ZLINK_EXPORT int zlink_device (int type_, void *frontend_, void *backend_);
ZLINK_EXPORT int zlink_sendmsg (void *s_, zlink_msg_t *msg_, int flags_);
ZLINK_EXPORT int zlink_recvmsg (void *s_, zlink_msg_t *msg_, int flags_);
struct iovec;
ZLINK_EXPORT int
zlink_sendiov (void *s_, struct iovec *iov_, size_t count_, int flags_);
ZLINK_EXPORT int
zlink_recviov (void *s_, struct iovec *iov_, size_t *count_, int flags_);

/******************************************************************************/
/*  Encryption functions                                                      */
/******************************************************************************/

/*  Encode data with Z85 encoding. Returns encoded data                       */
ZLINK_EXPORT char *
zlink_z85_encode (char *dest_, const uint8_t *data_, size_t size_);

/*  Decode data with Z85 encoding. Returns decoded data                       */
ZLINK_EXPORT uint8_t *zlink_z85_decode (uint8_t *dest_, const char *string_);

/*  Generate z85-encoded public and private keypair with libsodium. */
/*  Returns 0 on success.                                                     */
ZLINK_EXPORT int zlink_curve_keypair (char *z85_public_key_, char *z85_secret_key_);

/*  Derive the z85-encoded public key from the z85-encoded secret key.        */
/*  Returns 0 on success.                                                     */
ZLINK_EXPORT int zlink_curve_public (char *z85_public_key_,
                                 const char *z85_secret_key_);

/******************************************************************************/
/*  Atomic utility methods                                                    */
/******************************************************************************/

ZLINK_EXPORT void *zlink_atomic_counter_new (void);
ZLINK_EXPORT void zlink_atomic_counter_set (void *counter_, int value_);
ZLINK_EXPORT int zlink_atomic_counter_inc (void *counter_);
ZLINK_EXPORT int zlink_atomic_counter_dec (void *counter_);
ZLINK_EXPORT int zlink_atomic_counter_value (void *counter_);
ZLINK_EXPORT void zlink_atomic_counter_destroy (void **counter_p_);

/******************************************************************************/
/*  Scheduling timers                                                         */
/******************************************************************************/

#define ZLINK_HAVE_TIMERS

typedef void (zlink_timer_fn) (int timer_id, void *arg);

ZLINK_EXPORT void *zlink_timers_new (void);
ZLINK_EXPORT int zlink_timers_destroy (void **timers_p);
ZLINK_EXPORT int
zlink_timers_add (void *timers, size_t interval, zlink_timer_fn handler, void *arg);
ZLINK_EXPORT int zlink_timers_cancel (void *timers, int timer_id);
ZLINK_EXPORT int
zlink_timers_set_interval (void *timers, int timer_id, size_t interval);
ZLINK_EXPORT int zlink_timers_reset (void *timers, int timer_id);
ZLINK_EXPORT long zlink_timers_timeout (void *timers);
ZLINK_EXPORT int zlink_timers_execute (void *timers);


/******************************************************************************/
/*  These functions are not documented by man pages -- use at your own risk.  */
/*  If you need these to be part of the formal ZLINK API, then (a) write a man  */
/*  page, and (b) write a test case in tests.                                 */
/******************************************************************************/

/*  Helper functions are used by perf tests so that they don't have to care   */
/*  about minutiae of time-related functions on different OS platforms.       */

/*  Starts the stopwatch. Returns the handle to the watch.                    */
ZLINK_EXPORT void *zlink_stopwatch_start (void);

/*  Returns the number of microseconds elapsed since the stopwatch was        */
/*  started, but does not stop or deallocate the stopwatch.                   */
ZLINK_EXPORT unsigned long zlink_stopwatch_intermediate (void *watch_);

/*  Stops the stopwatch. Returns the number of microseconds elapsed since     */
/*  the stopwatch was started, and deallocates that watch.                    */
ZLINK_EXPORT unsigned long zlink_stopwatch_stop (void *watch_);

/*  Sleeps for specified number of seconds.                                   */
ZLINK_EXPORT void zlink_sleep (int seconds_);

typedef void (zlink_thread_fn) (void *);

/* Start a thread. Returns a handle to the thread.                            */
ZLINK_EXPORT void *zlink_threadstart (zlink_thread_fn *func_, void *arg_);

/* Wait for thread to complete then free up resources.                        */
ZLINK_EXPORT void zlink_threadclose (void *thread_);


/******************************************************************************/
/*  These functions are DRAFT and disabled in stable releases, and subject to */
/*  change at ANY time until declared stable.                                 */
/******************************************************************************/

#ifdef ZLINK_BUILD_DRAFT_API

/*  DRAFT Socket types.                                                       */
#define ZLINK_SERVER 12
#define ZLINK_CLIENT 13
#define ZLINK_RADIO 14
#define ZLINK_DISH 15
#define ZLINK_GATHER 16
#define ZLINK_SCATTER 17
#define ZLINK_DGRAM 18
#define ZLINK_PEER 19
#define ZLINK_CHANNEL 20

/*  DRAFT Socket options.                                                     */
#define ZLINK_ZAP_ENFORCE_DOMAIN 93
#define ZLINK_LOOPBACK_FASTPATH 94
#define ZLINK_METADATA 95
#define ZLINK_MULTICAST_LOOP 96
#define ZLINK_ROUTER_NOTIFY 97
#define ZLINK_XPUB_MANUAL_LAST_VALUE 98
#define ZLINK_IN_BATCH_SIZE 101
#define ZLINK_OUT_BATCH_SIZE 102
#define ZLINK_WSS_KEY_PEM 103
#define ZLINK_WSS_CERT_PEM 104
#define ZLINK_WSS_TRUST_PEM 105
#define ZLINK_WSS_HOSTNAME 106
#define ZLINK_WSS_TRUST_SYSTEM 107
#define ZLINK_ONLY_FIRST_SUBSCRIBE 108
#define ZLINK_RECONNECT_STOP 109
#define ZLINK_HELLO_MSG 110
#define ZLINK_DISCONNECT_MSG 111
#define ZLINK_PRIORITY 112
#define ZLINK_BUSY_POLL 113
#define ZLINK_HICCUP_MSG 114
#define ZLINK_XSUB_VERBOSE_UNSUBSCRIBE 115
#define ZLINK_TOPICS_COUNT 116
#define ZLINK_NORM_MODE 117
#define ZLINK_NORM_UNICAST_NACK 118
#define ZLINK_NORM_BUFFER_SIZE 119
#define ZLINK_NORM_SEGMENT_SIZE 120
#define ZLINK_NORM_BLOCK_SIZE 121
#define ZLINK_NORM_NUM_PARITY 122
#define ZLINK_NORM_NUM_AUTOPARITY 123
#define ZLINK_NORM_PUSH 124

/*  DRAFT ZLINK_NORM_MODE options                                               */
#define ZLINK_NORM_FIXED 0
#define ZLINK_NORM_CC 1
#define ZLINK_NORM_CCL 2
#define ZLINK_NORM_CCE 3
#define ZLINK_NORM_CCE_ECNONLY 4

/*  DRAFT ZLINK_RECONNECT_STOP options                                          */
#define ZLINK_RECONNECT_STOP_CONN_REFUSED 0x1
#define ZLINK_RECONNECT_STOP_HANDSHAKE_FAILED 0x2
#define ZLINK_RECONNECT_STOP_AFTER_DISCONNECT 0x4

/*  DRAFT Context options                                                     */
#define ZLINK_ZERO_COPY_RECV 10

/*  DRAFT Context methods.                                                    */
ZLINK_EXPORT int zlink_ctx_set_ext (void *context_,
                                int option_,
                                const void *optval_,
                                size_t optvallen_);
ZLINK_EXPORT int zlink_ctx_get_ext (void *context_,
                                int option_,
                                void *optval_,
                                size_t *optvallen_);

/*  DRAFT Socket methods.                                                     */
ZLINK_EXPORT int zlink_join (void *s, const char *group);
ZLINK_EXPORT int zlink_leave (void *s, const char *group);
ZLINK_EXPORT uint32_t zlink_connect_peer (void *s_, const char *addr_);

/*  DRAFT Msg methods.                                                        */
ZLINK_EXPORT int zlink_msg_set_routing_id (zlink_msg_t *msg, uint32_t routing_id);
ZLINK_EXPORT uint32_t zlink_msg_routing_id (zlink_msg_t *msg);
ZLINK_EXPORT int zlink_msg_set_group (zlink_msg_t *msg, const char *group);
ZLINK_EXPORT const char *zlink_msg_group (zlink_msg_t *msg);
ZLINK_EXPORT int
zlink_msg_init_buffer (zlink_msg_t *msg_, const void *buf_, size_t size_);

/*  DRAFT Msg property names.                                                 */
#define ZLINK_MSG_PROPERTY_ROUTING_ID "Routing-Id"
#define ZLINK_MSG_PROPERTY_SOCKET_TYPE "Socket-Type"
#define ZLINK_MSG_PROPERTY_USER_ID "User-Id"
#define ZLINK_MSG_PROPERTY_PEER_ADDRESS "Peer-Address"

/*  Router notify options                                                     */
#define ZLINK_NOTIFY_CONNECT 1
#define ZLINK_NOTIFY_DISCONNECT 2

/******************************************************************************/
/*  Poller polling on sockets,fd and thread-safe sockets                      */
/******************************************************************************/

#define ZLINK_HAVE_POLLER

typedef struct zlink_poller_event_t
{
    void *socket;
    zlink_fd_t fd;
    void *user_data;
    short events;
} zlink_poller_event_t;

ZLINK_EXPORT void *zlink_poller_new (void);
ZLINK_EXPORT int zlink_poller_destroy (void **poller_p);
ZLINK_EXPORT int zlink_poller_size (void *poller);
ZLINK_EXPORT int
zlink_poller_add (void *poller, void *socket, void *user_data, short events);
ZLINK_EXPORT int zlink_poller_modify (void *poller, void *socket, short events);
ZLINK_EXPORT int zlink_poller_remove (void *poller, void *socket);
ZLINK_EXPORT int
zlink_poller_wait (void *poller, zlink_poller_event_t *event, long timeout);
ZLINK_EXPORT int zlink_poller_wait_all (void *poller,
                                    zlink_poller_event_t *events,
                                    int n_events,
                                    long timeout);
ZLINK_EXPORT int zlink_poller_fd (void *poller, zlink_fd_t *fd);

ZLINK_EXPORT int
zlink_poller_add_fd (void *poller, zlink_fd_t fd, void *user_data, short events);
ZLINK_EXPORT int zlink_poller_modify_fd (void *poller, zlink_fd_t fd, short events);
ZLINK_EXPORT int zlink_poller_remove_fd (void *poller, zlink_fd_t fd);

ZLINK_EXPORT int zlink_socket_get_peer_state (void *socket,
                                          const void *routing_id,
                                          size_t routing_id_size);

/*  DRAFT Socket monitoring events                                            */
#define ZLINK_EVENT_PIPES_STATS 0x10000

#define ZLINK_CURRENT_EVENT_VERSION 1
#define ZLINK_CURRENT_EVENT_VERSION_DRAFT 2

#define ZLINK_EVENT_ALL_V1 ZLINK_EVENT_ALL
#define ZLINK_EVENT_ALL_V2 ZLINK_EVENT_ALL_V1 | ZLINK_EVENT_PIPES_STATS

ZLINK_EXPORT int zlink_socket_monitor_versioned (
  void *s_, const char *addr_, uint64_t events_, int event_version_, int type_);
ZLINK_EXPORT int zlink_socket_monitor_pipes_stats (void *s);

#if !defined _WIN32
ZLINK_EXPORT int zlink_ppoll (zlink_pollitem_t *items_,
                          int nitems_,
                          long timeout_,
                          const sigset_t *sigmask_);
#else
// Windows has no sigset_t
ZLINK_EXPORT int zlink_ppoll (zlink_pollitem_t *items_,
                          int nitems_,
                          long timeout_,
                          const void *sigmask_);
#endif

#endif // ZLINK_BUILD_DRAFT_API


#undef ZLINK_EXPORT

#ifdef __cplusplus
}
#endif

#endif
