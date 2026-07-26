package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway

import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes

class CourierDirectory {
    private val actors = mutableMapOf<String, CourierBinding>()

    fun remember(ensured: EnsureCourierActorRes, sessionRoute: String): CourierBinding =
        synchronized(actors) {
            val binding = CourierBinding(ensured.courierId, ensured.actor, sessionRoute)
            actors[ensured.courierId] = binding
            binding
        }

    fun require(courierId: String): CourierBinding =
        synchronized(actors) { actors[courierId] }
            ?: throw IllegalStateException("Courier actor is not bound: $courierId")
}
