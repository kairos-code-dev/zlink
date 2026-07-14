package systems.zlink.samples.kotlin.supportchat.server.configuration

import org.springframework.boot.context.properties.ConfigurationProperties
import systems.zlink.contracts.core.RoutingId

@ConfigurationProperties("sample")
data class SampleTopology(
    val redisEndpoint: String?,
    val redisKeyPrefix: String?,
    val logDirectory: String?,
    val apiChannelEndpoint: String?,
    val apiHttpEndpoint: String?,
    val supportChannelEndpoint: String?,
    val sessionSpotEndpoint: String?,
    val sessionRouterEndpoint: String?,
    val supportEntrySpotEndpoint: String?,
    val supportEntrySpotRouterEndpoint: String?,
    val streamEndpoint: String?,
    val sessionRouterRid: String?,
    val sessionPubRid: String?,
    val supportEntryRid: String?,
) {
    fun location(): SampleLocation = SampleLocation(
        redisEndpoint = required(redisEndpoint, "redisEndpoint"),
        redisKeyPrefix = required(redisKeyPrefix, "redisKeyPrefix"),
    )

    fun logDirectory(): String = required(logDirectory, "logDirectory")

    fun api(): ApiTopology = ApiTopology(
        channelEndpoint = required(apiChannelEndpoint, "apiChannelEndpoint"),
        httpEndpoint = required(apiHttpEndpoint, "apiHttpEndpoint"),
    )

    fun session(): SampleSessionNode = SampleSessionNode(
        pubEndpoint = required(sessionSpotEndpoint, "sessionSpotEndpoint"),
        routerEndpoint = required(sessionRouterEndpoint, "sessionRouterEndpoint"),
        streamEndpoint = required(streamEndpoint, "streamEndpoint"),
        routingId = RoutingId.from(required(sessionRouterRid, "sessionRouterRid")),
        publisherRoutingId = RoutingId.from(required(sessionPubRid, "sessionPubRid")),
    )

    fun support(): SupportTopology = SupportTopology(
        channelEndpoint = required(supportChannelEndpoint, "supportChannelEndpoint"),
        entrySpotEndpoint = required(supportEntrySpotEndpoint, "supportEntrySpotEndpoint"),
        entryRouterEndpoint = required(supportEntrySpotRouterEndpoint, "supportEntrySpotRouterEndpoint"),
        entryRoutingId = RoutingId.from(required(supportEntryRid, "supportEntryRid")),
    )

    companion object {
        fun configPath(args: Array<String>): String {
            require(args.size == 2 && args[0] == "--config" && args[1].isNotBlank()) {
                "Usage: <role executable> --config <path>"
            }
            return args[1]
        }

        private fun required(value: String?, name: String): String =
            requireNotNull(value?.takeIf { it.isNotBlank() }) { "sample.$name is required" }
    }
}

data class SampleLocation(val redisEndpoint: String, val redisKeyPrefix: String)

data class ApiTopology(val channelEndpoint: String, val httpEndpoint: String)

data class SupportTopology(
    val channelEndpoint: String,
    val entrySpotEndpoint: String,
    val entryRouterEndpoint: String,
    val entryRoutingId: RoutingId,
)

data class SampleSessionNode(
    val pubEndpoint: String,
    val routerEndpoint: String,
    val streamEndpoint: String,
    val routingId: RoutingId,
    val publisherRoutingId: RoutingId,
)
