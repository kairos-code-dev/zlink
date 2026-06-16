package systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow.domain

import com.fasterxml.jackson.databind.JsonNode
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.CommerceStore.AuthorizePaymentResult
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.CommerceStore.ReserveInventoryResult
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.CommerceStore.StoredEvent
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.OrderLineInput
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.OrderStatuses
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderWorkflowReq

/**
 * Order aggregate rebuilt from the event stream. Owns the status state machine,
 * command dedupe, and compensation rules. Knows nothing about framework,
 * transport, or store implementation.
 */
class OrderAggregate private constructor(private val orderId: String) {
    private val processedCommands = HashSet<String>()
    private var status: String? = null
    private var reservationId: String? = null
    private var lines: List<OrderLineInput> = emptyList()

    companion object {
        fun rehydrate(orderId: String, stored: List<StoredEvent>, decode: (JsonNode, Class<*>) -> Any): OrderAggregate {
            val aggregate = OrderAggregate(orderId)
            for (event in stored) {
                aggregate.apply(event, decode)
            }
            return aggregate
        }
    }

    private fun apply(event: StoredEvent, decode: (JsonNode, Class<*>) -> Any) {
        when (event.eventType) {
            OrderEventTypes.OrderStarted -> {
                val started = decode(event.payload, OrderStartedEvent::class.java) as OrderStartedEvent
                status = OrderStatuses.Created
                lines = started.lines
                processedCommands.add(started.sourceCommandId)
            }
            OrderEventTypes.InventoryReserved -> {
                status = OrderStatuses.InventoryReserved
                reservationId = event.payload.path("reservationId").asText(null)
            }
            OrderEventTypes.InventoryReservationFailed -> status = OrderStatuses.Failed
            OrderEventTypes.PaymentAuthorized -> status = OrderStatuses.PaymentAuthorized
            OrderEventTypes.PaymentFailed -> status = OrderStatuses.Failed
            OrderEventTypes.OrderConfirmed -> status = OrderStatuses.Confirmed
            OrderEventTypes.OrderFailed -> status = OrderStatuses.Failed
        }
    }

    fun hasStarted(): Boolean = status != null

    fun isTerminal(): Boolean =
        status == OrderStatuses.Confirmed || status == OrderStatuses.Failed

    fun status(): String? = status

    fun lines(): List<OrderLineInput> = lines

    fun hasProcessedCommand(sourceCommandId: String): Boolean =
        processedCommands.contains(sourceCommandId)

    fun start(command: StartOrderWorkflowReq, eventId: String, now: Long): List<Any> {
        if (hasStarted()) {
            return emptyList()
        }
        return listOf(
            OrderStartedEvent(
                eventId,
                command.idempotencyKey,
                orderId,
                command.cartId,
                command.shippingAddressId,
                command.lines,
                command.amount,
                command.currency,
                now,
            ),
        )
    }

    fun applyInventoryResult(
        result: ReserveInventoryResult,
        reservedEventId: String,
        failedEventId: String,
        now: Long,
    ): List<Any> {
        if (isTerminal() || status != OrderStatuses.Created) {
            return emptyList()
        }
        if (!result.accepted) {
            val reason = result.reason ?: "inventory unavailable"
            return listOf(
                InventoryReservationFailedEvent(failedEventId, orderId, reason, now),
                OrderFailedEvent("$failedEventId-failed", orderId, reason, now),
            )
        }
        return listOf(InventoryReservedEvent(reservedEventId, orderId, result.reservationId!!, now))
    }

    fun applyPaymentResult(
        result: AuthorizePaymentResult,
        paymentEventId: String,
        releaseEventId: String,
        failedEventId: String,
        now: Long,
    ): List<Any> {
        if (isTerminal() || status != OrderStatuses.InventoryReserved) {
            return emptyList()
        }
        if (!result.accepted) {
            val reason = result.reason ?: "payment failed"
            return listOf(
                PaymentFailedEvent(paymentEventId, orderId, reason, now),
                InventoryReleasedEvent(releaseEventId, orderId, reservationId!!, reason, now),
                OrderFailedEvent(failedEventId, orderId, reason, now),
            )
        }
        return listOf(PaymentAuthorizedEvent(paymentEventId, orderId, result.paymentId!!, now))
    }

    fun confirm(eventId: String, now: Long): List<Any> {
        if (isTerminal() || status != OrderStatuses.PaymentAuthorized) {
            return emptyList()
        }
        return listOf(OrderConfirmedEvent(eventId, orderId, now))
    }
}
