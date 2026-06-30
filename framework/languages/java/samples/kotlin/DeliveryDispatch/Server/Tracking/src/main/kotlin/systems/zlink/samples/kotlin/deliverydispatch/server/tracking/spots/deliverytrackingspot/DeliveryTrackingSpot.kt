package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.deliverytrackingspot

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.actors.CustomerActor
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreateReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreateRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoinReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoinRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedReq

class DeliveryTrackingSpot(
    private val context: ZLinkSpotContext,
    private val directory: DeliverySpotDirectory,
    private val json: ObjectMapper,) : ZLinkSuspendingSpot<CustomerActor>() {
    private val customers = LinkedHashMap<String, CustomerActor>()
    private val history = mutableListOf<DeliveryStatusChangedReq>()
    private var deliveryId: String = ""

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse {
        val create = request.decode(DeliverySpotCreateReq::class.java)
        deliveryId = create.deliveryId
        directory.add(deliveryId, this)
        return ZLinkSpotCreateResponse.accept(DeliverySpotCreateRes(deliveryId))
    }

    override suspend fun onActorJoinSuspending(
        actor: CustomerActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        val join = request.decode(DeliverySpotJoinReq::class.java)
        if (join.deliveryId != deliveryId) {
            return ZLinkSpotActorJoinResponse.reject()
        }
        customers[actor.actorId()] = actor
        System.err.println(
            "deliverydispatch tracking spot: joined delivery=${join.deliveryId} customer=${actor.actorId()}",
        )
        return ZLinkSpotActorJoinResponse.accept(DeliverySpotJoinRes(join.deliveryId, actor.actorId()))
    }

    override fun onLeaveActor(actor: CustomerActor, cancellationToken: CancellationToken) {
        customers.remove(actor.actorId())
    }

    fun record(status: DeliveryStatusChangedReq) {
        history.add(status)
    }
}
