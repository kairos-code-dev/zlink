package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway

import systems.zlink.framework.actors.ZLinkActorRefSnapshot

data class CourierBinding(
    val courierId: String,
    val actor: ZLinkActorRefSnapshot,
    val sessionRoute: String,
)
