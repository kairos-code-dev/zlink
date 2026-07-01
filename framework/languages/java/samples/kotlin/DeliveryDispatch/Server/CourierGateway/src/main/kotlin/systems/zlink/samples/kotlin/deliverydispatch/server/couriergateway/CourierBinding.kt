package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway

import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ActorRefSnapshot

data class CourierBinding(
    val courierId: String,
    val actor: ActorRefSnapshot,
    val sessionRoute: String,
)
