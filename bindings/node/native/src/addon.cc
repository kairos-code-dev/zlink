#include "addon_api.h"

static napi_value init(napi_env env, napi_value exports)
{
    napi_property_descriptor descs[] = {
        {"version", 0, version, 0, 0, 0, napi_default, 0},
        {"ctxNew", 0, ctx_new, 0, 0, 0, napi_default, 0},
        {"ctxTerm", 0, ctx_term, 0, 0, 0, napi_default, 0},
        {"socketNew", 0, socket_new, 0, 0, 0, napi_default, 0},
        {"socketClose", 0, socket_close, 0, 0, 0, napi_default, 0},
        {"socketBind", 0, socket_bind, 0, 0, 0, napi_default, 0},
        {"socketConnect", 0, socket_connect, 0, 0, 0, napi_default, 0},
        {"socketSend", 0, socket_send, 0, 0, 0, napi_default, 0},
        {"socketRecv", 0, socket_recv, 0, 0, 0, napi_default, 0},

        {"registryNew", 0, registry_new, 0, 0, 0, napi_default, 0},
        {"registrySetEndpoints", 0, registry_set_endpoints, 0, 0, 0, napi_default, 0},
        {"registrySetId", 0, registry_set_id, 0, 0, 0, napi_default, 0},
        {"registryAddPeer", 0, registry_add_peer, 0, 0, 0, napi_default, 0},
        {"registrySetHeartbeat", 0, registry_set_heartbeat, 0, 0, 0, napi_default, 0},
        {"registrySetBroadcastInterval", 0, registry_set_broadcast, 0, 0, 0, napi_default, 0},
        {"registryStart", 0, registry_start, 0, 0, 0, napi_default, 0},
        {"registryDestroy", 0, registry_destroy, 0, 0, 0, napi_default, 0},

        {"discoveryNew", 0, discovery_new, 0, 0, 0, napi_default, 0},
        {"discoveryConnectRegistry", 0, discovery_connect, 0, 0, 0, napi_default, 0},
        {"discoverySubscribe", 0, discovery_subscribe, 0, 0, 0, napi_default, 0},
        {"discoveryUnsubscribe", 0, discovery_unsubscribe, 0, 0, 0, napi_default, 0},
        {"discoveryProviderCount", 0, discovery_provider_count, 0, 0, 0, napi_default, 0},
        {"discoveryServiceAvailable", 0, discovery_service_available, 0, 0, 0, napi_default, 0},
        {"discoveryGetProviders", 0, discovery_get_providers, 0, 0, 0, napi_default, 0},
        {"discoveryDestroy", 0, discovery_destroy, 0, 0, 0, napi_default, 0},

        {"gatewayNew", 0, gateway_new, 0, 0, 0, napi_default, 0},
        {"gatewaySend", 0, gateway_send, 0, 0, 0, napi_default, 0},
        {"gatewayRecv", 0, gateway_recv, 0, 0, 0, napi_default, 0},
        {"gatewaySetLbStrategy", 0, gateway_set_lb, 0, 0, 0, napi_default, 0},
        {"gatewaySetTlsClient", 0, gateway_set_tls, 0, 0, 0, napi_default, 0},
        {"gatewayConnectionCount", 0, gateway_connection_count, 0, 0, 0, napi_default, 0},
        {"gatewayDestroy", 0, gateway_destroy, 0, 0, 0, napi_default, 0},

        {"providerNew", 0, provider_new, 0, 0, 0, napi_default, 0},
        {"providerBind", 0, provider_bind, 0, 0, 0, napi_default, 0},
        {"providerConnectRegistry", 0, provider_connect_registry, 0, 0, 0, napi_default, 0},
        {"providerRegister", 0, provider_register, 0, 0, 0, napi_default, 0},
        {"providerUpdateWeight", 0, provider_update_weight, 0, 0, 0, napi_default, 0},
        {"providerUnregister", 0, provider_unregister, 0, 0, 0, napi_default, 0},
        {"providerRegisterResult", 0, provider_register_result, 0, 0, 0, napi_default, 0},
        {"providerSetTlsServer", 0, provider_set_tls_server, 0, 0, 0, napi_default, 0},
        {"providerRouter", 0, provider_router, 0, 0, 0, napi_default, 0},
        {"providerDestroy", 0, provider_destroy, 0, 0, 0, napi_default, 0},

        {"spotNodeNew", 0, spot_node_new, 0, 0, 0, napi_default, 0},
        {"spotNodeDestroy", 0, spot_node_destroy, 0, 0, 0, napi_default, 0},
        {"spotNodeBind", 0, spot_node_bind, 0, 0, 0, napi_default, 0},
        {"spotNodeConnectRegistry", 0, spot_node_connect_registry, 0, 0, 0, napi_default, 0},
        {"spotNodeConnectPeerPub", 0, spot_node_connect_peer, 0, 0, 0, napi_default, 0},
        {"spotNodeDisconnectPeerPub", 0, spot_node_disconnect_peer, 0, 0, 0, napi_default, 0},
        {"spotNodeRegister", 0, spot_node_register, 0, 0, 0, napi_default, 0},
        {"spotNodeUnregister", 0, spot_node_unregister, 0, 0, 0, napi_default, 0},
        {"spotNodeSetDiscovery", 0, spot_node_set_discovery, 0, 0, 0, napi_default, 0},
        {"spotNodeSetTlsServer", 0, spot_node_set_tls_server, 0, 0, 0, napi_default, 0},
        {"spotNodeSetTlsClient", 0, spot_node_set_tls_client, 0, 0, 0, napi_default, 0},
        {"spotNodePubSocket", 0, spot_node_pub_socket, 0, 0, 0, napi_default, 0},
        {"spotNodeSubSocket", 0, spot_node_sub_socket, 0, 0, 0, napi_default, 0},

        {"spotNew", 0, spot_new, 0, 0, 0, napi_default, 0},
        {"spotDestroy", 0, spot_destroy, 0, 0, 0, napi_default, 0},
        {"spotTopicCreate", 0, spot_topic_create, 0, 0, 0, napi_default, 0},
        {"spotTopicDestroy", 0, spot_topic_destroy, 0, 0, 0, napi_default, 0},
        {"spotPublish", 0, spot_publish, 0, 0, 0, napi_default, 0},
        {"spotSubscribe", 0, spot_subscribe, 0, 0, 0, napi_default, 0},
        {"spotSubscribePattern", 0, spot_subscribe_pattern, 0, 0, 0, napi_default, 0},
        {"spotUnsubscribe", 0, spot_unsubscribe, 0, 0, 0, napi_default, 0},
        {"spotRecv", 0, spot_recv, 0, 0, 0, napi_default, 0},
        {"spotPubSocket", 0, spot_pub_socket, 0, 0, 0, napi_default, 0},
        {"spotSubSocket", 0, spot_sub_socket, 0, 0, 0, napi_default, 0},

        {"monitorOpen", 0, monitor_open, 0, 0, 0, napi_default, 0},
        {"monitorRecv", 0, monitor_recv, 0, 0, 0, napi_default, 0},

        {"poll", 0, poll, 0, 0, 0, napi_default, 0},
    };

    napi_define_properties(env, exports, sizeof(descs) / sizeof(*descs), descs);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
