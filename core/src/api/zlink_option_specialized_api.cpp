/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"
#include "api/config_result_internal.hpp"
#include "api/zlink_option_internal.hpp"
#include "services/spot/spot_node_access.hpp"

extern "C" int zlink_socket_request_reply_set_default_timeout (
  void *socket_,
  const void *optval_,
  size_t optvallen_);
extern "C" int zlink_socket_request_reply_get_default_timeout (
  void *socket_,
  void *optval_,
  size_t *optvallen_);
extern "C" int zlink_spot_request_reply_set_default_timeout (
  void *spot_,
  const void *optval_,
  size_t optvallen_);
extern "C" int zlink_spot_request_reply_get_default_timeout (
  void *spot_,
  void *optval_,
  size_t *optvallen_);

zlink_config_result_t zlink_set_router_option (void *handle_,
                                              zlink_router_option_t option_,
                                              const void *optval_,
                                              size_t optvallen_)
{
    if (option_ == ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS) {
        zlink::socket_base_t *socket = as_socket (handle_);
        if (!socket || socket_type_of (socket) != ZLINK_CORE_SOCKET_ROUTER) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          zlink_socket_request_reply_set_default_timeout (handle_, optval_,
                                                          optvallen_));
    }

    const int socket_option = map_router_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_DEALER
            && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        if (type != ZLINK_CORE_SOCKET_ROUTER
            && type != ZLINK_CORE_SOCKET_DEALER) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          socket->setsockopt (socket_option, optval_, optvallen_));
    }
    errno = 0;

    return zlink::config_result_internal::from_rc (
      zlink_service_set_router_option (handle_, option_, socket_option,
                                       optval_, optvallen_));
}

zlink_config_result_t zlink_get_router_option (void *handle_,
                                              zlink_router_option_t option_,
                                              void *optval_,
                                              size_t *optvallen_)
{
    if (option_ == ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS) {
        zlink::socket_base_t *socket = as_socket (handle_);
        if (!socket || socket_type_of (socket) != ZLINK_CORE_SOCKET_ROUTER) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          zlink_socket_request_reply_get_default_timeout (handle_, optval_,
                                                          optvallen_));
    }

    const int socket_option = map_router_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_DEALER
            && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        if (type != ZLINK_CORE_SOCKET_ROUTER
            && type != ZLINK_CORE_SOCKET_DEALER) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          socket->getsockopt (socket_option, optval_, optvallen_));
    }
    errno = 0;

    return zlink::config_result_internal::from_rc (
      zlink_service_get_router_option (handle_, option_, socket_option, optval_,
                                       optvallen_));
}

zlink_config_result_t zlink_set_dealer_option (void *handle_,
                                              zlink_dealer_option_t option_,
                                              const void *optval_,
                                              size_t optvallen_)
{
    if (option_ == ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS) {
        zlink::socket_base_t *socket = as_socket (handle_);
        if (!socket || socket_type_of (socket) != ZLINK_CORE_SOCKET_DEALER) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          zlink_socket_request_reply_set_default_timeout (handle_, optval_,
                                                          optvallen_));
    }

    const int socket_option = map_dealer_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (set_socket_option_checked (
      socket, socket_type_of (socket), ZLINK_CORE_SOCKET_DEALER,
      ZLINK_CORE_SOCKET_DEALER, socket_option, optval_, optvallen_));
}

zlink_config_result_t zlink_set_stream_option (void *handle_,
                                              zlink_stream_option_t option_,
                                              const void *optval_,
                                              size_t optvallen_)
{
    const int socket_option = map_stream_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (set_socket_option_checked (
      socket, socket_type_of (socket), ZLINK_CORE_SOCKET_STREAM,
      ZLINK_CORE_SOCKET_STREAM, socket_option, optval_, optvallen_));
}

zlink_config_result_t zlink_get_stream_option (void *handle_,
                                              zlink_stream_option_t option_,
                                              void *optval_,
                                              size_t *optvallen_)
{
    const int socket_option = map_stream_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (get_socket_option_checked (
      socket, socket_type_of (socket), ZLINK_CORE_SOCKET_STREAM,
      ZLINK_CORE_SOCKET_STREAM, socket_option, optval_, optvallen_));
}

zlink_config_result_t zlink_set_spot_option (void *handle_,
                                            zlink_spot_option_t option_,
                                            const void *optval_,
                                            size_t optvallen_)
{
    if (option_ != ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    if (!as_spot_handle (handle_)) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    return zlink::config_result_internal::from_rc (
      zlink_spot_request_reply_set_default_timeout (handle_, optval_,
                                                    optvallen_));
}

zlink_config_result_t zlink_get_spot_option (void *handle_,
                                            zlink_spot_option_t option_,
                                            void *optval_,
                                            size_t *optvallen_)
{
    if (option_ != ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    if (!as_spot_handle (handle_)) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    return zlink::config_result_internal::from_rc (
      zlink_spot_request_reply_get_default_timeout (handle_, optval_,
                                                    optvallen_));
}

zlink_config_result_t zlink_set_pub_option (void *handle_,
                                           zlink_pub_option_t option_,
                                           const void *optval_,
                                           size_t optvallen_)
{
    const int socket_option = map_pub_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        return zlink::config_result_internal::from_rc (set_socket_option_checked (
          socket, socket_type_of (socket), ZLINK_CORE_SOCKET_PUB,
          ZLINK_CORE_SOCKET_XPUB, socket_option, optval_, optvallen_));
    }
    errno = 0;

    return zlink::config_result_internal::from_rc (
      zlink_service_set_pub_option (handle_, option_, socket_option, optval_,
                                    optvallen_));
}

zlink_config_result_t zlink_get_pub_option (void *handle_,
                                           zlink_pub_option_t option_,
                                           void *optval_,
                                           size_t *optvallen_)
{
    const int socket_option = map_pub_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        return zlink::config_result_internal::from_rc (get_socket_option_checked (
          socket, socket_type_of (socket), ZLINK_CORE_SOCKET_PUB,
          ZLINK_CORE_SOCKET_XPUB, socket_option, optval_, optvallen_));
    }
    errno = 0;

    return zlink::config_result_internal::from_rc (
      zlink_service_get_pub_option (handle_, option_, socket_option, optval_,
                                    optvallen_));
}

zlink_config_result_t zlink_set_sub_option (void *handle_,
                                           zlink_sub_option_t option_,
                                           const void *optval_,
                                           size_t optvallen_)
{
    const int socket_option = map_sub_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          socket->setsockopt (socket_option, optval_, optvallen_));
    }
    errno = 0;

    return zlink::config_result_internal::from_rc (
      zlink_service_set_sub_option (handle_, option_, socket_option, optval_,
                                    optvallen_));
}

zlink_config_result_t zlink_get_sub_option (void *handle_,
                                           zlink_sub_option_t option_,
                                           void *optval_,
                                           size_t *optvallen_)
{
    const int socket_option = map_sub_option (option_);
    if (socket_option < 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        return zlink::config_result_internal::from_rc (get_socket_option_checked (
          socket, socket_type_of (socket), ZLINK_CORE_SOCKET_SUB,
          ZLINK_CORE_SOCKET_XSUB, socket_option, optval_, optvallen_));
    }
    errno = 0;

    return zlink::config_result_internal::from_rc (
      zlink_service_get_sub_option (handle_, option_, socket_option, optval_,
                                    optvallen_));
}

zlink_config_result_t zlink_set_spot_node_option (void *handle_,
                                                 zlink_spot_node_option_t option_,
                                                 const void *optval_,
                                                 size_t optvallen_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (handle_);
    if (!node)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::set_node_option (
        node, option_, optval_, optvallen_));
}

zlink_config_result_t zlink_get_spot_node_option (void *handle_,
                                                 zlink_spot_node_option_t option_,
                                                 void *optval_,
                                                 size_t *optvallen_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (handle_);
    if (!node)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (
      zlink::spot_node_access_t::get_node_option (
        node, option_, optval_, optvallen_));
}
