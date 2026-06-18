package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain

import com.fasterxml.jackson.databind.JsonNode
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderState
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderStatuses

/**
 * Pure projection folder: applies a stored event to the running order state.
 * The projection is rebuildable from the event stream alone.
 */
object OrderProjection {
    fun apply(current: OrderState?, eventType: String, payload: JsonNode): OrderState {
        val ts = payload.path("createdAtUnixMs").asLong(
            payload.path("confirmedAtUnixMs").asLong(
                payload.path("failedAtUnixMs").asLong(System.currentTimeMillis()),
            ),
        )
        return when (eventType) {
            OrderEventTypes.OrderStarted -> OrderState(
                payload.path("orderId").asText(),
                OrderStatuses.Created,
                payload.path("shippingAddressId").asText(null),
                null,
                null,
                null,
                if (payload.has("amount")) payload.path("amount").asDouble() else null,
                payload.path("currency").asText(null),
                ts,
            )
            OrderEventTypes.InventoryReserved -> require(current).copy(
                status = OrderStatuses.InventoryReserved,
                reservationId = payload.path("reservationId").asText(null),
                updatedAtUnixMs = ts,
            )
            OrderEventTypes.InventoryReservationFailed -> require(current).copy(
                status = OrderStatuses.Failed,
                reason = payload.path("reason").asText(null),
                updatedAtUnixMs = ts,
            )
            OrderEventTypes.PaymentAuthorized -> require(current).copy(
                status = OrderStatuses.PaymentAuthorized,
                paymentId = payload.path("paymentId").asText(null),
                updatedAtUnixMs = ts,
            )
            OrderEventTypes.PaymentFailed -> require(current).copy(
                status = OrderStatuses.Failed,
                reason = payload.path("reason").asText(null),
                updatedAtUnixMs = ts,
            )
            OrderEventTypes.InventoryReleased -> require(current).copy(
                reason = payload.path("reason").asText(null),
                updatedAtUnixMs = ts,
            )
            OrderEventTypes.OrderConfirmed -> require(current).copy(
                status = OrderStatuses.Confirmed,
                updatedAtUnixMs = ts,
            )
            OrderEventTypes.OrderFailed -> require(current).copy(
                status = OrderStatuses.Failed,
                reason = payload.path("reason").asText(null),
                updatedAtUnixMs = ts,
            )
            else -> require(current)
        }
    }

    private fun require(current: OrderState?): OrderState =
        current ?: throw IllegalStateException("Cannot apply order event before OrderStartedEvent.")
}
