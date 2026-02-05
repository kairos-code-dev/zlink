#ifndef __PLATFORM_HPP_INCLUDED__
#define __PLATFORM_HPP_INCLUDED__

#define ZLINK_HAVE_WINDOWS
#define ZLINK_HAVE_WINDOWS_TARGET_XP

#define ZLINK_BUILD_DRAFT_API

#define ZLINK_USE_SELECT
#define FD_SETSIZE 1024

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"Iphlpapi.lib")

#endif
