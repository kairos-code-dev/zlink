package systems.zlink.samples.kotlin.deliverydispatch.server.dispatch

import java.time.Instant
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.locations.ZLinkSpotAddress
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatus
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionRes

class DispatchWorker(
    private val channels: ZLinkClient,
    private val routes: ZLinkRouteClient,
) {
    fun dispatch(request: AssignDelivery) {
        if (request.deliveryId == "delivery-reassign") {
            dispatchReassigned(request)
            return
        }
        dispatchSuccessful(request)
    }

    fun assertServerEvidence(request: ServerAssertionReq): ServerAssertionRes =
        channels
            .requestToChannel(SampleNames.TrackingChannel, request)
            .await(ServerAssertionRes::class.java)

    private fun dispatchSuccessful(request: AssignDelivery) {
        publishStatus(request.deliveryId, DeliveryStatus.Assigned, "courier-a")
        val offered = offer(request, "courier-a")
        if (!offered.accepted) {
            publishStatus(request.deliveryId, DeliveryStatus.Failed, "courier-a")
            return
        }
        publishStatus(request.deliveryId, DeliveryStatus.Accepted, "courier-a")
        publishStatus(request.deliveryId, DeliveryStatus.PickedUp, "courier-a")
        publishStatus(request.deliveryId, DeliveryStatus.Delivered, "courier-a")
    }

    private fun dispatchReassigned(request: AssignDelivery) {
        publishStatus(request.deliveryId, DeliveryStatus.Assigned, "courier-a")
        val first = offer(request, "courier-a")
        if (first.accepted) {
            publishStatus(request.deliveryId, DeliveryStatus.Accepted, "courier-a")
            publishStatus(request.deliveryId, DeliveryStatus.Delivered, "courier-a")
            return
        }
        publishStatus(request.deliveryId, DeliveryStatus.Reassigned, "courier-b")
        val second = offer(request, "courier-b")
        if (!second.accepted) {
            publishStatus(request.deliveryId, DeliveryStatus.Failed, "courier-b")
            return
        }
        publishStatus(request.deliveryId, DeliveryStatus.Accepted, "courier-b")
        publishStatus(request.deliveryId, DeliveryStatus.Delivered, "courier-b")
    }

    private fun offer(request: AssignDelivery, courierId: String): OfferDeliveryRes {
        val address = courierAddress(courierId)
        val found = routes
            .requestToSpot(SampleNames.CourierSpotMesh, address, FindCourierActorReq(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .await(FindCourierActorRes::class.java)
        if (found.actor == null) {
            return OfferDeliveryRes(
                deliveryId = request.deliveryId,
                courierId = courierId,
                accepted = false,
                reason = "courier actor is not bound: $courierId",
            )
        }
        return routes
            .requestToSpot(
                SampleNames.CourierSpotMesh,
                address,
                OfferDeliveryReq(
                    courierId = courierId,
                    deliveryId = request.deliveryId,
                    pickupAddress = request.pickupAddress,
                    dropoffAddress = request.dropoffAddress,
                ),
            )
            .timeout(SampleTimings.OfferRequestTimeout)
            .await(OfferDeliveryRes::class.java)
    }

    private fun courierAddress(courierId: String): ZLinkSpotAddress {
        val nodeRid = RoutingId.from(SampleTopology.courierPlacement(courierId))
        return ZLinkSpotAddress(SampleNames.CourierSpotMesh, nodeRid, nodeRid)
    }

    private fun publishStatus(
        deliveryId: String,
        status: DeliveryStatus,
        courierId: String,
    ) {
        channels
            .requestToChannel(
                SampleNames.TrackingChannel,
                DeliveryStatusChangedReq(
                    deliveryId = deliveryId,
                    status = status,
                    courierId = courierId,
                    occurredAt = Instant.now(),
                ),
            )
            .await(DeliveryStatusChangedRes::class.java)
    }
}
