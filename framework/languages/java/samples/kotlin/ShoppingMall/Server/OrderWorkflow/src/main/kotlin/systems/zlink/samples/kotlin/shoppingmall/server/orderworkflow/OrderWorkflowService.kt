package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow

import com.fasterxml.jackson.databind.JsonNode
import java.util.UUID
import org.springframework.stereotype.Component
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.CommerceStore.StoredEvent
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.InventoryReleasedEvent
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.InventoryReservationResult
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.OrderAggregate
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.OrderEventTypes
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.OrderProjection
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.OrderStartedEvent
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.PaymentAuthorizationResult
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.domain.StoredOrderEvent
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderState
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderStatuses
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.StartOrderWorkflowReq

/**
 * Application service owning the event-sourced order workflow: loads the event
 * stream, rebuilds the aggregate, appends events with optimistic version
 * checks, advances the saga, and refreshes the projection.
 */
@Component
class OrderWorkflowService(private val store: CommerceStore) {
    private val json = store.json
    private val decode: (JsonNode, Class<*>) -> Any = { payload, type -> json.convertValue(payload, type) }

    fun start(command: StartOrderWorkflowReq): OrderState {
        val stored = store.readEvents(command.orderId)
        val aggregate = OrderAggregate.rehydrate(command.orderId, stored.toDomainEvents(), decode)
        if (aggregate.hasProcessedCommand(command.idempotencyKey)) {
            return requireProjection(command.orderId)
        }
        val events = aggregate.start(command, newEventId("started", command.orderId), now())
        if (events.isNotEmpty()) {
            appendAndProject(command.orderId, stored.size.toLong(), events)
        }
        store.markIdempotencyStarted(command.idempotencyKey)
        return requireProjection(command.orderId)
    }

    fun continueWorkflow(orderId: String): OrderState {
        val paymentMethodId = store.orderPaymentMethod(orderId)
        // Each iteration advances one event-stream transition; a started order
        // reaches a terminal state within a handful of steps.
        for (step in 0 until MAX_WORKFLOW_STEPS) {
            val stored = store.readEvents(orderId)
            val aggregate = OrderAggregate.rehydrate(orderId, stored.toDomainEvents(), decode)
            if (aggregate.isTerminal() || !aggregate.hasStarted()) {
                return store.findReadModel(orderId) ?: store.placeholder(orderId)
            }
            val status = aggregate.status()
            val ts = now()
            val next: List<Any> = when (status) {
                OrderStatuses.Created -> {
                    val reserve = store.reserveInventory(orderId, aggregate.lines())
                    aggregate.applyInventoryResult(
                        InventoryReservationResult(reserve.accepted, reserve.reservationId, reserve.reason),
                        newEventId("reserved", orderId),
                        newEventId("inv-fail", orderId),
                        ts,
                    )
                }
                OrderStatuses.InventoryReserved -> {
                    val payment = store.authorizePayment(
                        orderId,
                        paymentMethodId ?: "",
                        amountFromStarted(stored),
                        currencyFromStarted(stored),
                    )
                    aggregate.applyPaymentResult(
                        PaymentAuthorizationResult(payment.accepted, payment.paymentId, payment.reason),
                        newEventId("paid", orderId),
                        newEventId("released", orderId),
                        newEventId("pay-fail", orderId),
                        ts,
                    )
                }
                OrderStatuses.PaymentAuthorized -> aggregate.confirm(newEventId("confirmed", orderId), ts)
                else -> emptyList()
            }
            if (next.isEmpty()) {
                return requireProjection(orderId)
            }
            appendAndProject(orderId, stored.size.toLong(), next)
            for (event in next) {
                if (event is InventoryReleasedEvent) {
                    store.releaseInventory(orderId, event.reservationId, event.reason)
                }
            }
        }
        return store.findReadModel(orderId) ?: store.placeholder(orderId)
    }

    fun rebuildProjection(orderId: String): OrderState {
        val stored = store.readEvents(orderId)
        if (stored.isEmpty()) {
            throw IllegalStateException("No order event stream for '$orderId'.")
        }
        var state: OrderState? = null
        for (event in stored) {
            state = OrderProjection.apply(state, event.eventType, event.payload)
        }
        store.saveReadModel(state!!)
        return state
    }

    private fun appendAndProject(orderId: String, expectedVersion: Long, events: List<Any>) {
        val toStore = events.map { toStored(orderId, it) }
        store.appendEvents(orderId, expectedVersion, toStore)
        var state: OrderState? = store.findReadModel(orderId)
        for (stored in store.readEvents(orderId)) {
            state = OrderProjection.apply(state, stored.eventType, stored.payload)
        }
        store.saveReadModel(state!!)
    }

    private fun toStored(orderId: String, event: Any): StoredEvent {
        val payload = json.valueToTree<JsonNode>(event)
        val eventType = event.javaClass.simpleName
        val eventId = payload.path("eventId").asText(newEventId("evt", orderId))
        val sourceCommandId = if (event is OrderStartedEvent) event.sourceCommandId else null
        val createdAt = payload.path("createdAtUnixMs").asLong(
            payload.path("confirmedAtUnixMs").asLong(
                payload.path("failedAtUnixMs").asLong(now()),
            ),
        )
        return StoredEvent(eventId, sourceCommandId, orderId, eventType, payload, 0, createdAt)
    }

    private fun List<StoredEvent>.toDomainEvents(): List<StoredOrderEvent> =
        map {
            StoredOrderEvent(
                it.eventId,
                it.sourceCommandId,
                it.orderId,
                it.eventType,
                it.payload,
                it.version,
                it.createdAtUnixMs,
            )
        }

    private fun requireProjection(orderId: String): OrderState =
        store.findReadModel(orderId)
            ?: throw IllegalStateException("Projection missing for '$orderId'.")

    private fun amountFromStarted(stored: List<StoredEvent>): Double {
        for (event in stored) {
            if (event.eventType == OrderEventTypes.OrderStarted) {
                return event.payload.path("amount").asDouble()
            }
        }
        return 0.0
    }

    private fun currencyFromStarted(stored: List<StoredEvent>): String {
        for (event in stored) {
            if (event.eventType == OrderEventTypes.OrderStarted) {
                return event.payload.path("currency").asText("USD")
            }
        }
        return "USD"
    }

    private fun newEventId(prefix: String, orderId: String): String =
        "$prefix-$orderId-${UUID.randomUUID().toString().replace("-", "")}"

    private fun now(): Long = System.currentTimeMillis()

    private companion object {
        const val MAX_WORKFLOW_STEPS = 8
    }
}
