/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_core_options.h"

#include <algorithm>
#include <string>

namespace {

static const int32_t k_legacy_opt_routing_id = 5;
static const int32_t k_legacy_opt_subscribe = 6;
static const int32_t k_legacy_opt_unsubscribe = 7;
static const int32_t k_legacy_opt_xpub_verbose = 40;

} // namespace

int set_socket_option(void *sock, int32_t opt, const void *data, size_t len)
{
    switch (opt) {
    case k_legacy_opt_routing_id:
        return zlink_set_routing_id(sock, data, len);
    case k_legacy_opt_subscribe: {
        std::string filter(static_cast<const char *>(data), len);
        return zlink_set_subscription(sock, filter.c_str());
    }
    case k_legacy_opt_unsubscribe: {
        std::string filter(static_cast<const char *>(data), len);
        return zlink_unset_subscription(sock, filter.c_str());
    }
    case k_legacy_opt_xpub_verbose: {
        int value = 0;
        if (len >= sizeof(int))
            memcpy(&value, data, sizeof(int));
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_VERBOSE, &value,
                                    sizeof(value));
    }
    case ZLINK_ROUTER_OPT_MANDATORY:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_MANDATORY, data,
                                       len);
    case ZLINK_ROUTER_OPT_PROBE:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_PROBE, data, len);
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                                       data, len);
    case ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
                                       data, len);
    case ZLINK_ROUTER_OPT_WEIGHT:
        return zlink_set_router_option(sock, ZLINK_ROUTER_OPT_WEIGHT, data, len);
    case ZLINK_DEALER_OPT_PROBE:
        return zlink_set_dealer_option(sock, ZLINK_DEALER_OPT_PROBE, data, len);
    case ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS:
        return zlink_set_dealer_option(sock, ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
                                       data, len);
    case ZLINK_DEALER_OPT_WEIGHT:
        return zlink_set_dealer_option(sock, ZLINK_DEALER_OPT_WEIGHT, data, len);
    case ZLINK_STREAM_OPT_NOTIFY:
        return zlink_set_stream_option(sock, ZLINK_STREAM_OPT_NOTIFY, data, len);
    case ZLINK_PUB_OPT_VERBOSE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_VERBOSE, data, len);
    case ZLINK_PUB_OPT_VERBOSER:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_VERBOSER, data, len);
    case ZLINK_PUB_OPT_MANUAL:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_MANUAL, data, len);
    case ZLINK_PUB_OPT_MANUAL_LAST_VALUE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_MANUAL_LAST_VALUE, data,
                                    len);
    case ZLINK_PUB_OPT_NODROP:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_NODROP, data, len);
    case ZLINK_PUB_OPT_WELCOME_MSG:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_WELCOME_MSG, data, len);
    case ZLINK_PUB_OPT_TOPICS_COUNT:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_TOPICS_COUNT, data, len);
    case ZLINK_PUB_OPT_APPROVE_SUBSCRIBE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, data,
                                    len);
    case ZLINK_PUB_OPT_REJECT_SUBSCRIBE:
        return zlink_set_pub_option(sock, ZLINK_PUB_OPT_REJECT_SUBSCRIBE, data,
                                    len);
    case ZLINK_SUB_OPT_TOPICS_COUNT:
        return zlink_set_sub_option(sock, ZLINK_SUB_OPT_TOPICS_COUNT, data, len);
    default:
        return zlink_set_option(sock, static_cast<zlink_option_t>(opt), data,
                                len);
    }
}

int get_socket_option(void *sock, int32_t opt, void *data, size_t *len)
{
    switch (opt) {
    case k_legacy_opt_routing_id: {
        zlink_routing_id_t rid;
        memset(&rid, 0, sizeof(rid));
        int rc = zlink_get_routing_id(sock, &rid);
        if (rc != 0)
            return rc;
        size_t copy_len = std::min(*len, static_cast<size_t>(rid.size));
        if (copy_len > 0)
            memcpy(data, rid.data, copy_len);
        *len = rid.size;
        return 0;
    }
    case ZLINK_ROUTER_OPT_MANDATORY:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_MANDATORY, data,
                                       len);
    case ZLINK_ROUTER_OPT_PROBE:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_PROBE, data, len);
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                                       data, len);
    case ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
                                       data, len);
    case ZLINK_ROUTER_OPT_WEIGHT:
        return zlink_get_router_option(sock, ZLINK_ROUTER_OPT_WEIGHT, data, len);
    case ZLINK_DEALER_OPT_PROBE:
        return zlink_get_dealer_option(sock, ZLINK_DEALER_OPT_PROBE, data, len);
    case ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS:
        return zlink_get_dealer_option(sock, ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
                                       data, len);
    case ZLINK_DEALER_OPT_WEIGHT:
        return zlink_get_dealer_option(sock, ZLINK_DEALER_OPT_WEIGHT, data, len);
    case ZLINK_STREAM_OPT_NOTIFY:
        return zlink_get_stream_option(sock, ZLINK_STREAM_OPT_NOTIFY, data, len);
    case ZLINK_PUB_OPT_VERBOSE:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_VERBOSE, data, len);
    case ZLINK_PUB_OPT_VERBOSER:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_VERBOSER, data, len);
    case ZLINK_PUB_OPT_MANUAL:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_MANUAL, data, len);
    case ZLINK_PUB_OPT_MANUAL_LAST_VALUE:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_MANUAL_LAST_VALUE, data,
                                    len);
    case ZLINK_PUB_OPT_NODROP:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_NODROP, data, len);
    case ZLINK_PUB_OPT_WELCOME_MSG:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_WELCOME_MSG, data, len);
    case ZLINK_PUB_OPT_TOPICS_COUNT:
        return zlink_get_pub_option(sock, ZLINK_PUB_OPT_TOPICS_COUNT, data, len);
    case ZLINK_SUB_OPT_TOPICS_COUNT:
        return zlink_get_sub_option(sock, ZLINK_SUB_OPT_TOPICS_COUNT, data, len);
    default:
        return zlink_get_option(sock, static_cast<zlink_option_t>(opt), data,
                                len);
    }
}

size_t initial_getopt_buffer_len(int32_t opt)
{
    switch (opt) {
    case k_legacy_opt_routing_id:
    case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
        return 255;
    case ZLINK_OPT_LAST_ENDPOINT:
    case ZLINK_OPT_TLS_CERT:
    case ZLINK_OPT_TLS_KEY:
    case ZLINK_OPT_TLS_CA:
    case ZLINK_OPT_TLS_HOSTNAME:
    case ZLINK_OPT_TLS_PASSWORD:
    case ZLINK_OPT_BINDTODEVICE:
    case ZLINK_OPT_ZMP_METADATA:
        return 256;
    case ZLINK_OPT_MAXMSGSIZE:
        return sizeof(int64_t);
    default:
        return sizeof(int);
    }
}
