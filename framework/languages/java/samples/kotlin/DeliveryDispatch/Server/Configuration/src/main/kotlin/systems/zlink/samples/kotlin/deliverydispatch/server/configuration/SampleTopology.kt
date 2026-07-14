package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

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
        val RegistryPubEndpoint: String = property("registryPubEndpoint", "tcp://127.0.0.1:49101")
        val RegistryRouterEndpoint: String = property("registryRouterEndpoint", "tcp://127.0.0.1:49102")
        val TrackingChannelEndpoint: String = property("trackingChannelEndpoint", "tcp://127.0.0.1:49103")
        val TrackingSpotEndpoint: String = property("trackingSpotEndpoint", "tcp://127.0.0.1:49118")
        val CustomerStreamEndpoint: String = property("customerStreamEndpoint", "tcp://127.0.0.1:49104")
        val CourierStreamEndpoint: String = property("courierStreamEndpoint", "tcp://127.0.0.1:49105")
        val CourierGatewayChannelEndpoint: String = property("courierGatewayChannelEndpoint", "tcp://127.0.0.1:49106")
        val DispatchHttpEndpoint: String = property("dispatchHttpEndpoint", "http://127.0.0.1:49107")
        val DispatchChannelEndpoint: String =
            property("dispatchChannelEndpoint", "tcp://127.0.0.1:49121")
        val CustomerSpotEndpoint: String = property("customerSpotEndpoint", "tcp://127.0.0.1:49109")
        val CustomerSpotRouterEndpoint: String = property("customerSpotRouterEndpoint", "tcp://127.0.0.1:49110")
        val CustomerSpotNodeRid: String = property("customerSpotNodeRid", "customer-node-1")
        val CourierActorNode1Rid: String = property("courierActorNode1Rid", "courier-node-1")
        val CourierActorNode2Rid: String = property("courierActorNode2Rid", "courier-node-2")
        val CourierActorNode1SpotEndpoint: String =
            property("courierActorNode1SpotEndpoint", "tcp://127.0.0.1:49113")
        val CourierActorNode2SpotEndpoint: String =
            property("courierActorNode2SpotEndpoint", "tcp://127.0.0.1:49114")
        val CourierActorNode1RouterEndpoint: String =
            property("courierActorNode1RouterEndpoint", "tcp://127.0.0.1:49115")
        val CourierActorNode2RouterEndpoint: String =
            property("courierActorNode2RouterEndpoint", "tcp://127.0.0.1:49116")
        val CourierSessionSpotRouterEndpoint: String =
            property("courierSessionSpotRouterEndpoint", "tcp://127.0.0.1:49117")
        val CourierSessionSpotEndpoint: String =
            property("courierSessionSpotEndpoint", "tcp://127.0.0.1:49119")
        val CourierSessionSpotNodeRid: String = property("courierSessionSpotNodeRid", "courier-session-node")
        val RedisEndpoint: String = requiredProperty("redisEndpoint")
        val RedisKeyPrefix: String = property("redisKeyPrefix", "deliverydispatch:kotlin:")

        private fun property(name: String, fallback: String): String =
            System.getProperty("zlink.samples.deliverydispatch.$name", fallback)

        private fun requiredProperty(name: String): String {
            val value = System.getProperty("zlink.samples.deliverydispatch.$name")
            require(!value.isNullOrBlank()) { "Missing zlink.samples.deliverydispatch.$name" }
            return value
        }

        fun courierPlacement(courierId: String): String =
            if (courierId == "courier-b") CourierActorNode2Rid else CourierActorNode1Rid
    }
}
