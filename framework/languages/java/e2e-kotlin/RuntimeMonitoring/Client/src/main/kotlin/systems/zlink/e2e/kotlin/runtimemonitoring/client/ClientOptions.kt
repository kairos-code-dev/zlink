package systems.zlink.e2e.kotlin.runtimemonitoring.client

import systems.zlink.e2e.kotlin.runtimemonitoring.Env

class ClientOptions(
    val apiEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"),
    val handshakeEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_HANDSHAKE_ENDPOINT"),
    val filteredApiEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_FILTERED_API_ENDPOINT"),
    val throwingApiEndpoint: String = Env.get("ZLINK_KOTLIN_E2E_THROWING_API_ENDPOINT"),
    val serviceHttp: String = Env.get("ZLINK_KOTLIN_E2E_SERVICE_HTTP"),
    val filteredServiceHttp: String = Env.get("ZLINK_KOTLIN_E2E_FILTERED_SERVICE_HTTP"),
    val throwingServiceHttp: String = Env.get("ZLINK_KOTLIN_E2E_THROWING_SERVICE_HTTP"),
    val triggerHttp: String = Env.get("ZLINK_KOTLIN_E2E_TRIGGER_HTTP"),
    val logDir: String = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR"),
    val filteredServiceBin: String = Env.get("ZLINK_KOTLIN_E2E_FILTERED_SERVICE_BIN"),
    val scenario: String = Env.get("ZLINK_KOTLIN_E2E_SCENARIO", "all"),
)
