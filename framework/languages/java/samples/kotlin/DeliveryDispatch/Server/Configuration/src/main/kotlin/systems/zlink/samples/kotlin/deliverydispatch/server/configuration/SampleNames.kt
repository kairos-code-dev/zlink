package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

object SampleNames {
    const val CourierChannel: String = "deliverydispatch.courier"
    const val CourierActorNodeRouteChannel: String = "deliverydispatch.courier.actor-node"
    const val TrackingChannel: String = "deliverydispatch.tracking"
    const val CustomerRouteChannel: String = "deliverydispatch.customer"
    const val CustomerSpotMesh: String = "delivery-customers"
    const val CourierSpotMesh: String = "delivery-couriers"
    const val CustomerStreamNode: String = "deliverydispatch.customer.stream"
    const val CourierStreamNode: String = "deliverydispatch.courier.stream"
    const val CustomerActorType: String = "deliverydispatch.customer.actor"
    const val CourierActorType: String = "deliverydispatch.courier.actor"

    const val RegistryRole: String = "registry"
    const val DispatchRole: String = "dispatch"
    const val CourierGatewayRole: String = "couriergateway"
    const val CourierSessionRole: String = "couriersession"
    const val CourierSpotNodeRolePrefix: String = "courierspotnode"
    const val TrackingRole: String = "tracking"
    const val CustomerGatewayRole: String = "customergateway"

    const val TopologyReadyMarker: String = "topology=ready"
    const val ReassignmentMarker: String = "deliverydispatch-reassignment=completed"
    const val ServerEvidenceMarker: String = "deliverydispatch-server-evidence=completed"
    const val CompletedMarker: String = "deliverydispatch=completed"

    fun courierActorNodeChannel(nodeRid: String): String = "$CourierActorNodeRouteChannel.$nodeRid"
}
