package systems.zlink.e2e.kotlin.runtimemonitoring.client

import systems.zlink.e2e.kotlin.runtimemonitoring.Env

class ClientOptions(
    val apiEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"),
    val handshakeEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_HANDSHAKE_ENDPOINT"),
    val filteredApiEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_FILTERED_API_ENDPOINT"),
    val throwingApiEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_THROWING_API_ENDPOINT"),
    val registryHttp: String = Env.get("ZLINK_KOTLIN_E2E_REGISTRY_HTTP"),
    val serviceHttp: String = Env.get("ZLINK_KOTLIN_E2E_SERVICE_HTTP"),
    val failoverServiceHttp: String = Env.get("ZLINK_KOTLIN_E2E_FAILOVER_SERVICE_HTTP"),
    val filteredServiceHttp: String = Env.get("ZLINK_KOTLIN_E2E_FILTERED_SERVICE_HTTP"),
    val throwingServiceHttp: String = Env.get("ZLINK_KOTLIN_E2E_THROWING_SERVICE_HTTP"),
    val triggerHttp: String = Env.get("ZLINK_KOTLIN_E2E_TRIGGER_HTTP"),
    val registryRouter: String = Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"),
    val logDir: String = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR"),
    val filteredServiceBin: String = Env.get("ZLINK_KOTLIN_E2E_FILTERED_SERVICE_BIN"),
)
