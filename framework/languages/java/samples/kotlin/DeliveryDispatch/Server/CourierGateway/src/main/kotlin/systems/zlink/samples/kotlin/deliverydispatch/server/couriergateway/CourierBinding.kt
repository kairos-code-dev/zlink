package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway

import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ActorRefWire

data class CourierBinding(
    val courierId: String,
    val actor: ActorRefWire,
    val sessionRoute: String,
)
