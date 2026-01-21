/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_H_INCLUDED__
#define __ZMQ_H_INCLUDED__

/*  Version macros for compile-time API version detection                     */
#define ZMQ_VERSION_MAJOR 4
#define ZMQ_VERSION_MINOR 3
#define ZMQ_VERSION_PATCH 5

#define ZMQ_MAKE_VERSION(major, minor, patch)                                  \
    ((major) *10000 + (minor) *100 + (patch))
#define ZMQ_VERSION                                                            \
    ZMQ_MAKE_VERSION (ZMQ_VERSION_MAJOR, ZMQ_VERSION_MINOR, ZMQ_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

#if !defined _WIN32_WCE
#include <errno.h>
#endif
#include <stddef.h>
#include <stdio.h>

/*  Handle DSO symbol visibility                                             */
#if defined ZMQ_NO_EXPORT
#define ZMQ_EXPORT
#else
#if defined _WIN32
#if defined ZMQ_STATIC
#define ZMQ_EXPORT
#elif defined DLL_EXPORT
#define ZMQ_EXPORT __declspec(dllexport)
#else
#define ZMQ_EXPORT __declspec(dllimport)
#endif
#else
#if defined __SUNPRO_C || defined __SUNPRO_CC
#define ZMQ_EXPORT __global
#elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#define ZMQ_EXPORT __attribute__ ((visibility ("default")))
#else
#define ZMQ_EXPORT
#endif
#endif
#endif

/*  Define integer types needed for event interface                          */
#define ZMQ_DEFINED_STDINT 1
#if defined ZMQ_HAVE_SOLARIS || defined ZMQ_HAVE_OPENVMS
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
#include <signal.h>
#endif

#ifdef ZMQ_HAVE_AIX
#include <poll.h>
#endif

/******************************************************************************/
/*  0MQ errors.                                                               */
/******************************************************************************/
#define ZMQ_HAUSNUMERO 156384712

#ifndef ENOTSUP
#define ENOTSUP (ZMQ_HAUSNUMERO + 1)
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (ZMQ_HAUSNUMERO + 2)
#endif
#ifndef ENOBUFS
#define ENOBUFS (ZMQ_HAUSNUMERO + 3)
#endif
#ifndef ENETDOWN
#define ENETDOWN (ZMQ_HAUSNUMERO + 4)
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (ZMQ_HAUSNUMERO + 5)
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (ZMQ_HAUSNUMERO + 6)
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (ZMQ_HAUSNUMERO + 7)
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (ZMQ_HAUSNUMERO + 8)
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (ZMQ_HAUSNUMERO + 9)
#endif
#ifndef EMSGSIZE
#define EMSGSIZE (ZMQ_HAUSNUMERO + 10)
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT (ZMQ_HAUSNUMERO + 11)
#endif
#ifndef ENETUNREACH
#define ENETUNREACH (ZMQ_HAUSNUMERO + 12)
#endif
#ifndef ECONNABORTED
#define ECONNABORTED (ZMQ_HAUSNUMERO + 13)
#endif
#ifndef ECONNRESET
#define ECONNRESET (ZMQ_HAUSNUMERO + 14)
#endif
#ifndef ENOTCONN
#define ENOTCONN (ZMQ_HAUSNUMERO + 15)
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT (ZMQ_HAUSNUMERO + 16)
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH (ZMQ_HAUSNUMERO + 17)
#endif
#ifndef ENETRESET
#define ENETRESET (ZMQ_HAUSNUMERO + 18)
#endif

#define EFSM (ZMQ_HAUSNUMERO + 51)
#define ENOCOMPATPROTO (ZMQ_HAUSNUMERO + 52)
#define ETERM (ZMQ_HAUSNUMERO + 53)
#define EMTHREAD (ZMQ_HAUSNUMERO + 54)

ZMQ_EXPORT int zmq_errno (void);
ZMQ_EXPORT const char *zmq_strerror (int errnum_);
ZMQ_EXPORT void zmq_version (int *major_, int *minor_, int *patch_);

/******************************************************************************/
/*  0MQ infrastructure (a.k.a. context) initialisation & termination.         */
/******************************************************************************/
#define ZMQ_IO_THREADS 1
#define ZMQ_MAX_SOCKETS 2
#define ZMQ_SOCKET_LIMIT 3
#define ZMQ_THREAD_PRIORITY 3
#define ZMQ_THREAD_SCHED_POLICY 4
#define ZMQ_MAX_MSGSZ 5
#define ZMQ_MSG_T_SIZE 6
#define ZMQ_THREAD_AFFINITY_CPU_ADD 7
#define ZMQ_THREAD_AFFINITY_CPU_REMOVE 8
#define ZMQ_THREAD_NAME_PREFIX 9

#define ZMQ_IO_THREADS_DFLT 1
#define ZMQ_MAX_SOCKETS_DFLT 1023
#define ZMQ_THREAD_PRIORITY_DFLT -1
#define ZMQ_THREAD_SCHED_POLICY_DFLT -1

ZMQ_EXPORT void *zmq_ctx_new (void);
ZMQ_EXPORT int zmq_ctx_term (void *context_);
ZMQ_EXPORT int zmq_ctx_shutdown (void *context_);
ZMQ_EXPORT int zmq_ctx_set (void *context_, int option_, int optval_);
ZMQ_EXPORT int zmq_ctx_get (void *context_, int option_);

/******************************************************************************/
/*  0MQ message definition.                                                   */
/******************************************************************************/
typedef struct zmq_msg_t
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
} zmq_msg_t;

typedef void (zmq_free_fn) (void *data_, void *hint_);

ZMQ_EXPORT int zmq_msg_init (zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_init_size (zmq_msg_t *msg_, size_t size_);
ZMQ_EXPORT int zmq_msg_init_data (
  zmq_msg_t *msg_, void *data_, size_t size_, zmq_free_fn *ffn_, void *hint_);
ZMQ_EXPORT int zmq_msg_send (zmq_msg_t *msg_, void *s_, int flags_);
ZMQ_EXPORT int zmq_msg_recv (zmq_msg_t *msg_, void *s_, int flags_);
ZMQ_EXPORT int zmq_msg_close (zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_move (zmq_msg_t *dest_, zmq_msg_t *src_);
ZMQ_EXPORT int zmq_msg_copy (zmq_msg_t *dest_, zmq_msg_t *src_);
ZMQ_EXPORT void *zmq_msg_data (zmq_msg_t *msg_);
ZMQ_EXPORT size_t zmq_msg_size (const zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_more (const zmq_msg_t *msg_);
ZMQ_EXPORT int zmq_msg_get (const zmq_msg_t *msg_, int property_);
ZMQ_EXPORT int zmq_msg_set (zmq_msg_t *msg_, int property_, int optval_);
ZMQ_EXPORT const char *zmq_msg_gets (const zmq_msg_t *msg_,
                                     const char *property_);

/******************************************************************************/
/*  0MQ socket definition.                                                    */
/******************************************************************************/
#define ZMQ_PAIR 0
#define ZMQ_PUB 1
#define ZMQ_SUB 2
#define ZMQ_DEALER 5
#define ZMQ_ROUTER 6
#define ZMQ_XPUB 9
#define ZMQ_XSUB 10

#define ZMQ_AFFINITY 4
#define ZMQ_ROUTING_ID 5
#define ZMQ_SUBSCRIBE 6
#define ZMQ_UNSUBSCRIBE 7
#define ZMQ_RATE 8
#define ZMQ_RECOVERY_IVL 9
#define ZMQ_SNDBUF 11
#define ZMQ_RCVBUF 12
#define ZMQ_RCVMORE 13
#define ZMQ_FD 14
#define ZMQ_EVENTS 15
#define ZMQ_TYPE 16
#define ZMQ_LINGER 17
#define ZMQ_RECONNECT_IVL 18
#define ZMQ_BACKLOG 19
#define ZMQ_RECONNECT_IVL_MAX 21
#define ZMQ_MAXMSGSIZE 22
#define ZMQ_SNDHWM 23
#define ZMQ_RCVHWM 24
#define ZMQ_MULTICAST_HOPS 25
#define ZMQ_RCVTIMEO 27
#define ZMQ_SNDTIMEO 28
#define ZMQ_LAST_ENDPOINT 32
#define ZMQ_ROUTER_MANDATORY 33
#define ZMQ_TCP_KEEPALIVE 34
#define ZMQ_TCP_KEEPALIVE_CNT 35
#define ZMQ_TCP_KEEPALIVE_IDLE 36
#define ZMQ_TCP_KEEPALIVE_INTVL 37
#define ZMQ_IMMEDIATE 39
#define ZMQ_XPUB_VERBOSE 40
#define ZMQ_IPV6 42
#define ZMQ_MECHANISM 43
#define ZMQ_PLAIN_SERVER 44
#define ZMQ_PLAIN_USERNAME 45
#define ZMQ_PLAIN_PASSWORD 46
#define ZMQ_PROBE_ROUTER 51
#define ZMQ_CONFLATE 54
#define ZMQ_ZAP_DOMAIN 55
#define ZMQ_ROUTER_HANDOVER 56
#define ZMQ_TOS 57
#define ZMQ_CONNECT_ROUTING_ID 61
#define ZMQ_HANDSHAKE_IVL 66
#define ZMQ_XPUB_NODROP 69
#define ZMQ_BLOCKY 70
#define ZMQ_XPUB_MANUAL 71
#define ZMQ_XPUB_WELCOME_MSG 72
#define ZMQ_INVERT_MATCHING 74
#define ZMQ_HEARTBEAT_IVL 75
#define ZMQ_HEARTBEAT_TTL 76
#define ZMQ_HEARTBEAT_TIMEOUT 77
#define ZMQ_XPUB_VERBOSER 78
#define ZMQ_CONNECT_TIMEOUT 79
#define ZMQ_TCP_MAXRT 80
#define ZMQ_THREAD_SAFE 81
#define ZMQ_MULTICAST_MAXTPDU 84
#define ZMQ_USE_FD 89
#define ZMQ_BINDTODEVICE 92
#define ZMQ_XPUB_MANUAL_LAST_VALUE 98
#define ZMQ_ONLY_FIRST_SUBSCRIBE 108
#define ZMQ_TOPICS_COUNT 116

//  TLS protocol options
#define ZMQ_TLS_CERT 95
#define ZMQ_TLS_KEY 96
#define ZMQ_TLS_CA 97
#define ZMQ_TLS_VERIFY 98
#define ZMQ_TLS_REQUIRE_CLIENT_CERT 99
#define ZMQ_TLS_HOSTNAME 100
#define ZMQ_TLS_TRUST_SYSTEM 101
#define ZMQ_TLS_PASSWORD 102

#define ZMQ_MORE 1
#define ZMQ_SHARED 3

#define ZMQ_DONTWAIT 1
#define ZMQ_SNDMORE 2

#define ZMQ_NULL 0
#define ZMQ_PLAIN 1

/******************************************************************************/
/*  0MQ socket events and monitoring                                          */
/******************************************************************************/
#define ZMQ_EVENT_CONNECTED 0x0001
#define ZMQ_EVENT_CONNECT_DELAYED 0x0002
#define ZMQ_EVENT_CONNECT_RETRIED 0x0004
#define ZMQ_EVENT_LISTENING 0x0008
#define ZMQ_EVENT_BIND_FAILED 0x0010
#define ZMQ_EVENT_ACCEPTED 0x0020
#define ZMQ_EVENT_ACCEPT_FAILED 0x0040
#define ZMQ_EVENT_CLOSED 0x0080
#define ZMQ_EVENT_CLOSE_FAILED 0x0100
#define ZMQ_EVENT_DISCONNECTED 0x0200
#define ZMQ_EVENT_MONITOR_STOPPED 0x0400
#define ZMQ_EVENT_ALL 0xFFFF
#define ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL 0x0800
#define ZMQ_EVENT_HANDSHAKE_SUCCEEDED 0x1000
#define ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL 0x2000
#define ZMQ_EVENT_HANDSHAKE_FAILED_AUTH 0x4000

#define ZMQ_PROTOCOL_ERROR_ZMTP_UNSPECIFIED 0x10000000
#define ZMQ_PROTOCOL_ERROR_ZMTP_UNEXPECTED_COMMAND 0x10000001
#define ZMQ_PROTOCOL_ERROR_ZMTP_INVALID_SEQUENCE 0x10000002
#define ZMQ_PROTOCOL_ERROR_ZMTP_KEY_EXCHANGE 0x10000003
#define ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_UNSPECIFIED 0x10000011
#define ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_MESSAGE 0x10000012
#define ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_HELLO 0x10000013
#define ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_INITIATE 0x10000014
#define ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_ERROR 0x10000015
#define ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_READY 0x10000016
#define ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_WELCOME 0x10000017
#define ZMQ_PROTOCOL_ERROR_ZMTP_INVALID_METADATA 0x10000018
#define ZMQ_PROTOCOL_ERROR_ZMTP_CRYPTOGRAPHIC 0x11000001
#define ZMQ_PROTOCOL_ERROR_ZMTP_MECHANISM_MISMATCH 0x11000002
#define ZMQ_PROTOCOL_ERROR_ZAP_UNSPECIFIED 0x20000000
#define ZMQ_PROTOCOL_ERROR_ZAP_MALFORMED_REPLY 0x20000001
#define ZMQ_PROTOCOL_ERROR_ZAP_BAD_REQUEST_ID 0x20000002
#define ZMQ_PROTOCOL_ERROR_ZAP_BAD_VERSION 0x20000003
#define ZMQ_PROTOCOL_ERROR_ZAP_INVALID_STATUS_CODE 0x20000004
#define ZMQ_PROTOCOL_ERROR_ZAP_INVALID_METADATA 0x20000005
#define ZMQ_PROTOCOL_ERROR_WS_UNSPECIFIED 0x30000000

ZMQ_EXPORT void *zmq_socket (void *, int type_);
ZMQ_EXPORT int zmq_close (void *s_);
ZMQ_EXPORT int
zmq_setsockopt (void *s_, int option_, const void *optval_, size_t optvallen_);
ZMQ_EXPORT int
zmq_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_);
ZMQ_EXPORT int zmq_bind (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_connect (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_unbind (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_disconnect (void *s_, const char *addr_);
ZMQ_EXPORT int zmq_send (void *s_, const void *buf_, size_t len_, int flags_);
ZMQ_EXPORT int
zmq_send_const (void *s_, const void *buf_, size_t len_, int flags_);
ZMQ_EXPORT int zmq_recv (void *s_, void *buf_, size_t len_, int flags_);
ZMQ_EXPORT int zmq_socket_monitor (void *s_, const char *addr_, int events_);

#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zmq_fd_t;
#else
typedef unsigned int zmq_fd_t;
#endif
#else
typedef int zmq_fd_t;
#endif

#define ZMQ_POLLIN 1
#define ZMQ_POLLOUT 2
#define ZMQ_POLLERR 4
#define ZMQ_POLLPRI 8

typedef struct zmq_pollitem_t
{
    void *socket;
    zmq_fd_t fd;
    short events;
    short revents;
} zmq_pollitem_t;

#define ZMQ_POLLITEMS_DFLT 16

ZMQ_EXPORT int zmq_poll (zmq_pollitem_t *items_, int nitems_, long timeout_);

ZMQ_EXPORT int zmq_proxy (void *frontend_, void *backend_, void *capture_);
ZMQ_EXPORT int zmq_proxy_steerable (void *frontend_,
                                    void *backend_,
                                    void *capture_,
                                    void *control_);

ZMQ_EXPORT int zmq_has (const char *capability_);

/******************************************************************************/
/*  Atomic utility methods                                                    */
/******************************************************************************/
ZMQ_EXPORT void *zmq_atomic_counter_new (void);
void zmq_atomic_counter_set (void *counter_, int value_);
int zmq_atomic_counter_inc (void *counter_);
int zmq_atomic_counter_dec (void *counter_);
int zmq_atomic_counter_value (void *counter_);
void zmq_atomic_counter_destroy (void **counter_p_);

/******************************************************************************/
/*  Scheduling timers                                                         */
/******************************************************************************/
typedef void (zmq_timer_fn) (int timer_id, void *arg);

ZMQ_EXPORT void *zmq_timers_new (void);
ZMQ_EXPORT int zmq_timers_destroy (void **timers_p);
ZMQ_EXPORT int
zmq_timers_add (void *timers, size_t interval, zmq_timer_fn handler, void *arg);
ZMQ_EXPORT int zmq_timers_cancel (void *timers, int timer_id);
ZMQ_EXPORT int
zmq_timers_set_interval (void *timers, int timer_id, size_t interval);
ZMQ_EXPORT int zmq_timers_reset (void *timers, int timer_id);
ZMQ_EXPORT long zmq_timers_timeout (void *timers);
ZMQ_EXPORT int zmq_timers_execute (void *timers);

ZMQ_EXPORT void *zmq_stopwatch_start (void);
ZMQ_EXPORT unsigned long zmq_stopwatch_intermediate (void *watch_);
ZMQ_EXPORT unsigned long zmq_stopwatch_stop (void *watch_);
ZMQ_EXPORT void zmq_sleep (int seconds_);

typedef void (zmq_thread_fn) (void *);
ZMQ_EXPORT void *zmq_threadstart (zmq_thread_fn *func_, void *arg_);
ZMQ_EXPORT void zmq_threadclose (void *thread_);

#undef ZMQ_EXPORT

#ifdef __cplusplus
}
#endif

#endif
