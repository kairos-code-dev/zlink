/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"
#include "api/zlink_option_internal.hpp"

int zlink_set_router_option (void *handle_,
                             zlink_router_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int socket_option = map_router_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_DEALER
            && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return -1;
        }
        if (type != ZLINK_CORE_SOCKET_ROUTER
            && type != ZLINK_CORE_SOCKET_DEALER) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_set_router_option (handle_, option_, socket_option,
                                            optval_, optvallen_);
}

int zlink_get_router_option (void *handle_,
                             zlink_router_option_t option_,
                             void *optval_,
                             size_t *optvallen_)
{
    const int socket_option = map_router_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_DEALER
            && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return -1;
        }
        if (type != ZLINK_CORE_SOCKET_ROUTER
            && type != ZLINK_CORE_SOCKET_DEALER) {
            errno = EINVAL;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_get_router_option (handle_, option_, socket_option,
                                            optval_, optvallen_);
}

int zlink_set_dealer_option (void *handle_,
                             zlink_dealer_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int socket_option = map_dealer_option (option_);
    if (socket_option < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return set_socket_option_checked (
      socket, socket_type_of (socket), ZLINK_CORE_SOCKET_DEALER,
      ZLINK_CORE_SOCKET_DEALER, socket_option, optval_, optvallen_);
}

int zlink_set_stream_option (void *handle_,
                             zlink_stream_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int socket_option = map_stream_option (option_);
    if (socket_option < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return set_socket_option_checked (
      socket, socket_type_of (socket), ZLINK_CORE_SOCKET_STREAM,
      ZLINK_CORE_SOCKET_STREAM, socket_option, optval_, optvallen_);
}

int zlink_get_stream_option (void *handle_,
                             zlink_stream_option_t option_,
                             void *optval_,
                             size_t *optvallen_)
{
    const int socket_option = map_stream_option (option_);
    if (socket_option < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return get_socket_option_checked (
      socket, socket_type_of (socket), ZLINK_CORE_SOCKET_STREAM,
      ZLINK_CORE_SOCKET_STREAM, socket_option, optval_, optvallen_);
}

int zlink_set_pub_option (void *handle_,
                          zlink_pub_option_t option_,
                          const void *optval_,
                          size_t optvallen_)
{
    const int socket_option = map_pub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        return set_socket_option_checked (
          socket, socket_type_of (socket), ZLINK_CORE_SOCKET_PUB,
          ZLINK_CORE_SOCKET_XPUB, socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_set_pub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}

int zlink_get_pub_option (void *handle_,
                          zlink_pub_option_t option_,
                          void *optval_,
                          size_t *optvallen_)
{
    const int socket_option = map_pub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        return get_socket_option_checked (
          socket, socket_type_of (socket), ZLINK_CORE_SOCKET_PUB,
          ZLINK_CORE_SOCKET_XPUB, socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_get_pub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}

int zlink_set_sub_option (void *handle_,
                          zlink_sub_option_t option_,
                          const void *optval_,
                          size_t optvallen_)
{
    const int socket_option = map_sub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_set_sub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}

int zlink_get_sub_option (void *handle_,
                          zlink_sub_option_t option_,
                          void *optval_,
                          size_t *optvallen_)
{
    const int socket_option = map_sub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        return get_socket_option_checked (
          socket, socket_type_of (socket), ZLINK_CORE_SOCKET_SUB,
          ZLINK_CORE_SOCKET_XSUB, socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_get_sub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}
