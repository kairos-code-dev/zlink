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

@ZLinkPacket("CreateDeliveryReq")
data class CreateDeliveryReq(
    val deliveryId: String,
    val customerId: String,
    val pickupAddress: String,
    val dropoffAddress: String,
)

data class CreateDeliveryRes(val deliveryId: String)

@ZLinkPacket("AssignDeliveryReq")
data class AssignDeliveryReq(
    val deliveryId: String,
    val customerId: String,
    val pickupAddress: String,
    val dropoffAddress: String,
)

data class AssignDeliveryRes(val deliveryId: String, val courierId: String)

@ZLinkPacket("OfferDeliveryReq")
data class OfferDeliveryReq(
    val deliveryId: String,
    val pickupAddress: String,
    val dropoffAddress: String,
)

data class OfferDeliveryRes(
    val deliveryId: String,
    val courierId: String,
    val accepted: Boolean,
    val reason: String?,
)

@ZLinkPacket("DeliveryStatusChangedReq")
data class DeliveryStatusChangedReq(
    val deliveryId: String,
    val status: String,
    val courierId: String?,
    val occurredAtUnixMs: Long,
)

data class DeliveryStatusChangedRes(val deliveryId: String, val status: String)

@ZLinkPacket("EnsureCustomerActorReq")
data class EnsureCustomerActorReq(val customerId: String)

data class ActorRefSnapshot(val nodeRid: ByteArray, val actorId: String, val generation: Long)

data class EnsureCustomerActorRes(val customerId: String, val actor: ActorRefSnapshot)

@ZLinkPacket("SubscribeCustomerToDeliveryReq")
data class SubscribeCustomerToDeliveryReq(val customerId: String, val deliveryId: String)

data class SubscribeCustomerToDeliveryRes(val customerId: String, val deliveryId: String)

data class DeliverySpotCreateReq(val deliveryId: String)

data class DeliverySpotCreateRes(val deliveryId: String)

data class DeliverySpotJoinReq(val deliveryId: String, val customerId: String)

data class DeliverySpotJoinRes(val deliveryId: String, val customerId: String)

@ZLinkPacket("SubscribeDeliveryReq")
data class SubscribeDeliveryReq(val deliveryId: String)

@ZLinkPacket("SubscribeDeliveryRes")
data class SubscribeDeliveryRes(val deliveryId: String)

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
