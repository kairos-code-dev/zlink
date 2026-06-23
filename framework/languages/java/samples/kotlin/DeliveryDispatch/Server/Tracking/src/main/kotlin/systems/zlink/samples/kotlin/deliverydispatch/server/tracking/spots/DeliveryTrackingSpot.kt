package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.actors.CustomerActor
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreate
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreated
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoin
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoined
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChanged

class DeliveryTrackingSpot(
    private val context: ZLinkSpotContext,
    private val directory: DeliverySpotDirectory,
    private val json: ObjectMapper,) : ZLinkSuspendingSpot<CustomerActor>() {
    private val customers = LinkedHashMap<String, CustomerActor>()
    private val history = mutableListOf<DeliveryStatusChanged>()
    private var deliveryId: String = ""

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse {
        val create = request.decode(DeliverySpotCreate::class.java)
        deliveryId = create.deliveryId
        directory.add(deliveryId, this)
        return ZLinkSpotCreateResponse.accept(DeliverySpotCreated(deliveryId))
    }

    override suspend fun onActorJoinSuspending(
        actor: CustomerActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        val join = request.decode(DeliverySpotJoin::class.java)
        if (join.deliveryId != deliveryId) {
            return ZLinkSpotActorJoinResponse.reject()
        }
        customers[actor.actorId()] = actor
        System.err.println(
            "deliverydispatch tracking spot: joined delivery=${join.deliveryId} customer=${actor.actorId()}",
        )
        return ZLinkSpotActorJoinResponse.accept(DeliverySpotJoined(join.deliveryId, actor.actorId()))
    }

    override fun onLeaveActor(actor: CustomerActor, cancellationToken: CancellationToken) {
        customers.remove(actor.actorId())
    }

    fun record(status: DeliveryStatusChanged) {
        history.add(status)
    }
}
