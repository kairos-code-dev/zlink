/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_PLATFORM_HPP_INCLUDED__
#define __ZLINK_PLATFORM_HPP_INCLUDED__

//  This file provides the configuration for Linux, Windows, and OS/X
//  as determined by ZLINK_HAVE_XXX macros passed from project.gyp

//  Check that we're being called from our gyp makefile
#ifndef ZLINK_GYP_BUILD
#   error "foreign platform.hpp detected, please re-configure"
#endif

#if defined ZLINK_HAVE_WINDOWS
#   define ZLINK_USE_SELECT 1

#elif defined ZLINK_HAVE_OSX
#   define ZLINK_USE_KQUEUE 1
#   define HAVE_POSIX_MEMALIGN 1
#   define ZLINK_HAVE_IFADDRS 1
#   define ZLINK_HAVE_SO_KEEPALIVE 1
#   define ZLINK_HAVE_TCP_KEEPALIVE 1
#   define ZLINK_HAVE_TCP_KEEPCNT 1
#   define ZLINK_HAVE_TCP_KEEPINTVL 1
#   define ZLINK_HAVE_UIO 1
#   define HAVE_FORK 1

#elif defined ZLINK_HAVE_LINUX
#   define ZLINK_USE_EPOLL 1
#   define HAVE_POSIX_MEMALIGN 1
#   define ZLINK_HAVE_EVENTFD 1
#   define ZLINK_HAVE_IFADDRS 1
#   define ZLINK_HAVE_SOCK_CLOEXEC 1
#   define ZLINK_HAVE_SO_BINDTODEVICE 1
#   define ZLINK_HAVE_SO_KEEPALIVE 1
#   define ZLINK_HAVE_SO_PEERCRED 1
#   define ZLINK_HAVE_TCP_KEEPCNT 1
#   define ZLINK_HAVE_TCP_KEEPIDLE 1
#   define ZLINK_HAVE_TCP_KEEPINTVL 1
#   define ZLINK_HAVE_UIO 1
#   define HAVE_CLOCK_GETTIME 1
#   define HAVE_FORK 1
#   define HAVE_ACCEPT4 1

#else
#   error "No platform defined, abandoning"
#endif

#endif
