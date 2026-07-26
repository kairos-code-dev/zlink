package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

import java.nio.file.Files
import java.nio.file.Path
import java.util.Properties

data class SampleTopology(
    val registryUrl: String,
    val trackingUrl: String,
    val customerGatewayUrl: String,
    val courierSessionUrl: String,
    val courierSpotNode1Url: String,
    val courierSpotNode2Url: String,
    val courierGatewayUrl: String,
    val dispatchUrl: String,
) {
    fun roleUrls(): Map<String, String> =
        mapOf(
            SampleNames.RegistryRole to registryUrl,
            SampleNames.TrackingRole to trackingUrl,
            SampleNames.CustomerGatewayRole to customerGatewayUrl,
            SampleNames.CourierSessionRole to courierSessionUrl,
            "${SampleNames.CourierSpotNodeRolePrefix}1" to courierSpotNode1Url,
            "${SampleNames.CourierSpotNodeRolePrefix}2" to courierSpotNode2Url,
            SampleNames.CourierGatewayRole to courierGatewayUrl,
            SampleNames.DispatchRole to dispatchUrl,
        )

    companion object {
        lateinit var RegistryPubEndpoint: String
        lateinit var RegistryRouterEndpoint: String
        lateinit var TrackingChannelEndpoint: String
        lateinit var TrackingSpotEndpoint: String
        lateinit var CustomerStreamEndpoint: String
        lateinit var CourierStreamEndpoint: String
        lateinit var CourierGatewayChannelEndpoint: String
        lateinit var DispatchHttpEndpoint: String
        lateinit var DispatchChannelEndpoint: String
        lateinit var CustomerSpotEndpoint: String
        lateinit var CustomerSpotRouterEndpoint: String
        lateinit var CourierActorNode1SpotEndpoint: String
        lateinit var CourierActorNode2SpotEndpoint: String
        lateinit var CourierActorNode1RouterEndpoint: String
        lateinit var CourierActorNode2RouterEndpoint: String
        lateinit var CourierSessionSpotRouterEndpoint: String
        lateinit var CourierSessionSpotEndpoint: String
        lateinit var RedisEndpoint: String
        lateinit var RedisKeyPrefix: String
        lateinit var CourierNode: String
        lateinit var LogDirectory: String
        lateinit var StateDirectory: String

        fun configure(args: Array<String>) {
            require(args.size == 2 && args[0] == "--config" && args[1].isNotBlank()) {
                "Usage: <role> --config <path>"
            }
            val properties = Properties().also { values ->
                Files.newBufferedReader(Path.of(args[1])).use(values::load)
            }
            RegistryPubEndpoint = value(properties, "registryPubEndpoint", "tcp://127.0.0.1:49101")
            RegistryRouterEndpoint = value(properties, "registryRouterEndpoint", "tcp://127.0.0.1:49102")
            TrackingChannelEndpoint = value(properties, "trackingChannelEndpoint", "tcp://127.0.0.1:49103")
            TrackingSpotEndpoint = value(properties, "trackingSpotEndpoint", "tcp://127.0.0.1:49118")
            CustomerStreamEndpoint = value(properties, "customerStreamEndpoint", "tcp://127.0.0.1:49104")
            CourierStreamEndpoint = value(properties, "courierStreamEndpoint", "tcp://127.0.0.1:49105")
            CourierGatewayChannelEndpoint = value(properties, "courierGatewayChannelEndpoint", "tcp://127.0.0.1:49106")
            DispatchHttpEndpoint = value(properties, "dispatchHttpEndpoint", "http://127.0.0.1:49107")
            DispatchChannelEndpoint = value(properties, "dispatchChannelEndpoint", "tcp://127.0.0.1:49121")
            CustomerSpotEndpoint = value(properties, "customerSpotEndpoint", "tcp://127.0.0.1:49109")
            CustomerSpotRouterEndpoint = value(properties, "customerSpotRouterEndpoint", "tcp://127.0.0.1:49110")
            CourierActorNode1SpotEndpoint = value(properties, "courierActorNode1SpotEndpoint", "tcp://127.0.0.1:49113")
            CourierActorNode2SpotEndpoint = value(properties, "courierActorNode2SpotEndpoint", "tcp://127.0.0.1:49114")
            CourierActorNode1RouterEndpoint = value(properties, "courierActorNode1RouterEndpoint", "tcp://127.0.0.1:49115")
            CourierActorNode2RouterEndpoint = value(properties, "courierActorNode2RouterEndpoint", "tcp://127.0.0.1:49116")
            CourierSessionSpotRouterEndpoint = value(properties, "courierSessionSpotRouterEndpoint", "tcp://127.0.0.1:49117")
            CourierSessionSpotEndpoint = value(properties, "courierSessionSpotEndpoint", "tcp://127.0.0.1:49119")
            RedisEndpoint = required(properties, "redisEndpoint")
            RedisKeyPrefix = value(properties, "redisKeyPrefix", "deliverydispatch:kotlin:")
            CourierNode = value(properties, "courierNode", "node1")
            LogDirectory = required(properties, "logDirectory")
            StateDirectory = required(properties, "stateDirectory")
        }

        private fun value(properties: Properties, name: String, fallback: String): String =
            properties.getProperty(name)?.takeIf(String::isNotBlank) ?: fallback

        private fun required(properties: Properties, name: String): String =
            requireNotNull(properties.getProperty(name)?.takeIf(String::isNotBlank)) {
                "Missing DeliveryDispatch sample config: $name"
            }

    }
}
