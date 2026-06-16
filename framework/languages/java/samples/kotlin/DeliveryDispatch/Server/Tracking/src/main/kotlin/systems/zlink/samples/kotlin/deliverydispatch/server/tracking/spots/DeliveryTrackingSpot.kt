package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreate
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreated
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoin
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoined
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChanged

class DeliveryTrackingSpot(
    private val context: ZLinkSpotContext,
    private val directory: DeliverySpotDirectory,
    private val json: ObjectMapper,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpot(coroutines) {
    private val customers = LinkedHashMap<String, ZLinkActor>()
    private val history = mutableListOf<DeliveryStatusChanged>()
    private var deliveryId: String = ""

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: Message): ZLinkSpotCreateResponse {
        val create = json.readValue(request.toByteArray(), DeliverySpotCreate::class.java)
        deliveryId = create.deliveryId
        directory.add(deliveryId, this)
        return ZLinkSpotCreateResponse.accept(
            Message.from(json.writeValueAsBytes(DeliverySpotCreated(deliveryId))),
        )
    }

    override suspend fun onActorJoinSuspending(
        actor: ZLinkActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        val join = json.readValue(request.toByteArray(), DeliverySpotJoin::class.java)
        if (join.deliveryId != deliveryId) {
            return ZLinkSpotActorJoinResponse.reject()
        }
        customers[actor.actorId()] = actor
        System.err.println(
            "deliverydispatch tracking spot: joined delivery=${join.deliveryId} customer=${actor.actorId()}",
        )
        return ZLinkSpotActorJoinResponse.accept(
            Message.from(json.writeValueAsBytes(DeliverySpotJoined(join.deliveryId, actor.actorId()))),
        )
    }

    override fun onLeaveActor(actor: ZLinkActor, cancellationToken: CancellationToken) {
        customers.remove(actor.actorId())
    }

    fun record(status: DeliveryStatusChanged) {
        history.add(status)
    }
}
