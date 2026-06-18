package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain

import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderLineInput

/**
 * Order domain events. Each event type maps to its simple class name, which is
 * also the `eventType` discriminator in the store and the sequence the
 * self-check assertion compares against.
 */
object OrderEventTypes {
    const val OrderStarted = "OrderStartedEvent"
    const val InventoryReserved = "InventoryReservedEvent"
    const val InventoryReservationFailed = "InventoryReservationFailedEvent"
    const val PaymentAuthorized = "PaymentAuthorizedEvent"
    const val PaymentFailed = "PaymentFailedEvent"
    const val InventoryReleased = "InventoryReleasedEvent"
    const val OrderConfirmed = "OrderConfirmedEvent"
    const val OrderFailed = "OrderFailedEvent"
}

data class OrderStartedEvent(
    val eventId: String,
    val sourceCommandId: String,
    val orderId: String,
    val cartId: String,
    val shippingAddressId: String,
    val lines: List<OrderLineInput>,
    val amount: Double,
    val currency: String,
    val createdAtUnixMs: Long,
)

data class InventoryReservedEvent(
    val eventId: String,
    val orderId: String,
    val reservationId: String,
    val createdAtUnixMs: Long,
)

data class InventoryReservationFailedEvent(
    val eventId: String,
    val orderId: String,
    val reason: String,
    val createdAtUnixMs: Long,
)

data class PaymentAuthorizedEvent(
    val eventId: String,
    val orderId: String,
    val paymentId: String,
    val createdAtUnixMs: Long,
)

data class PaymentFailedEvent(
    val eventId: String,
    val orderId: String,
    val reason: String,
    val createdAtUnixMs: Long,
)

data class InventoryReleasedEvent(
    val eventId: String,
    val orderId: String,
    val reservationId: String,
    val reason: String,
    val createdAtUnixMs: Long,
)

data class OrderConfirmedEvent(
    val eventId: String,
    val orderId: String,
    val confirmedAtUnixMs: Long,
)

data class OrderFailedEvent(
    val eventId: String,
    val orderId: String,
    val reason: String,
    val failedAtUnixMs: Long,
)
