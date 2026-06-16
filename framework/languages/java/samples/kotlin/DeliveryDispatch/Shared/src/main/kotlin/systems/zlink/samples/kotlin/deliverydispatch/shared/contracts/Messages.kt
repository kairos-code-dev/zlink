package systems.zlink.samples.kotlin.deliverydispatch.shared.contracts

import systems.zlink.framework.handlers.ZLinkPacket

object DeliveryStatuses {
    const val Created = "Created"
    const val Assigned = "Assigned"
    const val Accepted = "Accepted"
    const val Reassigned = "Reassigned"
    const val PickedUp = "PickedUp"
    const val Delivered = "Delivered"
    const val Failed = "Failed"
}

@ZLinkPacket("CreateDeliveryRequest")
data class CreateDeliveryRequest(
    val deliveryId: String,
    val customerId: String,
    val pickupAddress: String,
    val dropoffAddress: String,
)

data class DeliveryCreated(val deliveryId: String)

@ZLinkPacket("AssignDelivery")
data class AssignDelivery(
    val deliveryId: String,
    val customerId: String,
    val pickupAddress: String,
    val dropoffAddress: String,
)

data class AssignDeliveryResult(val deliveryId: String, val courierId: String)

@ZLinkPacket("OfferDelivery")
data class OfferDelivery(
    val deliveryId: String,
    val pickupAddress: String,
    val dropoffAddress: String,
)

data class OfferDeliveryResult(
    val deliveryId: String,
    val courierId: String,
    val accepted: Boolean,
    val reason: String?,
)

@ZLinkPacket("DeliveryStatusChanged")
data class DeliveryStatusChanged(
    val deliveryId: String,
    val status: String,
    val courierId: String?,
    val occurredAtUnixMs: Long,
)

data class DeliveryStatusAck(val deliveryId: String, val status: String)

@ZLinkPacket("EnsureCustomerActor")
data class EnsureCustomerActor(val customerId: String)

data class ActorRefSnapshot(val nodeRid: ByteArray, val actorId: String, val generation: Long)

data class CustomerActorEnsured(val customerId: String, val actor: ActorRefSnapshot)

@ZLinkPacket("SubscribeCustomerToDelivery")
data class SubscribeCustomerToDelivery(val customerId: String, val deliveryId: String)

data class CustomerDeliverySubscribed(val customerId: String, val deliveryId: String)

data class DeliverySpotCreate(val deliveryId: String)

data class DeliverySpotCreated(val deliveryId: String)

data class DeliverySpotJoin(val deliveryId: String, val customerId: String)

data class DeliverySpotJoined(val deliveryId: String, val customerId: String)

@ZLinkPacket("SubscribeDelivery")
data class SubscribeDelivery(val deliveryId: String)

@ZLinkPacket("SubscribeDeliveryAccepted")
data class SubscribeDeliveryAccepted(val deliveryId: String)

@ZLinkPacket("DeliveryStatusNotify")
data class DeliveryStatusNotify(
    val deliveryId: String,
    val status: String,
    val courierId: String?,
    val occurredAtUnixMs: Long,
)

@ZLinkPacket("ServerAssertionReq")
data class ServerAssertionReq(
    val successfulDeliveryId: String,
    val reassignedDeliveryId: String,
)

data class ServerAssertionRes(val passed: Boolean, val evidence: List<String>)
