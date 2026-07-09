package systems.zlink.samples.kotlin.supportchat.server.configuration

import systems.zlink.contracts.core.RoutingId

data class SampleTopology(
    val redisEndpoint: String,
    val redisKeyPrefix: String,
    val apiChannelEndpoint: String,
    val apiHttpEndpoint: String,
    val supportChannelEndpoint: String,
    val sessionSpotEndpoint: String,
    val sessionRouterEndpoint: String,
    val supportEntrySpotEndpoint: String,
    val supportEntrySpotRouterEndpoint: String,
    val streamEndpoint: String,
    val sessionRouterRid: RoutingId,
    val sessionPubRid: RoutingId,
    val supportEntryRid: RoutingId,
) {
    val primarySession: SampleSessionNode
        get() = SampleSessionNode(
            pubEndpoint = sessionSpotEndpoint,
            routerEndpoint = sessionRouterEndpoint,
            streamEndpoint = streamEndpoint,
            routingId = sessionRouterRid,
            publisherRoutingId = sessionPubRid,
        )

    companion object {
        fun create(): SampleTopology =
            SampleTopology(
                redisEndpoint = readEndpoint("SUPPORTCHAT_REDIS_ENDPOINT", "127.0.0.1:6379"),
                redisKeyPrefix = readEndpoint("SUPPORTCHAT_REDIS_KEY_PREFIX", "supportchat:"),
                apiChannelEndpoint = readEndpoint("SUPPORTCHAT_API_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47203"),
                apiHttpEndpoint = readEndpoint("SUPPORTCHAT_API_HTTP_ENDPOINT", "http://127.0.0.1:47213"),
                supportChannelEndpoint = readEndpoint("SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47204"),
                sessionSpotEndpoint = readEndpoint("SUPPORTCHAT_SESSION_SPOT_ENDPOINT", "tcp://127.0.0.1:47205"),
                sessionRouterEndpoint = readEndpoint("SUPPORTCHAT_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47206"),
                supportEntrySpotEndpoint = readEndpoint("SUPPORTCHAT_ENTRY_SPOT_ENDPOINT", "tcp://127.0.0.1:47208"),
                supportEntrySpotRouterEndpoint = readEndpoint(
                    "SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT",
                    "tcp://127.0.0.1:47209",
                ),
                streamEndpoint = readEndpoint("SUPPORTCHAT_STREAM_ENDPOINT", "tcp://127.0.0.1:47212"),
                sessionRouterRid = RoutingId.from("3101"),
                sessionPubRid = RoutingId.from("3102"),
                supportEntryRid = RoutingId.from("4201"),
            )

        private fun readEndpoint(name: String, defaultValue: String): String =
            System.getenv(name)?.takeIf { it.isNotBlank() } ?: defaultValue
    }
}

data class SampleSessionNode(
    val pubEndpoint: String,
    val routerEndpoint: String,
    val streamEndpoint: String,
    val routingId: RoutingId,
    val publisherRoutingId: RoutingId,
)
