package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway

import systems.zlink.framework.actors.ActorRefSnapshot

data class CourierBinding(
    val courierId: String,
    val actor: ActorRefSnapshot,
    val sessionRoute: String,
)
