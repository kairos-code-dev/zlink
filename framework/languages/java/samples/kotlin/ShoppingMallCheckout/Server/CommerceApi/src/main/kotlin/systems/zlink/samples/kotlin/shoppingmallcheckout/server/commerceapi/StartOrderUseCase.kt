package systems.zlink.samples.kotlin.shoppingmallcheckout.server.commerceapi

import java.util.concurrent.locks.LockSupport
import kotlinx.coroutines.future.await
import org.springframework.stereotype.Component
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.SampleNames
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderReq
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderRes
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderWorkflowReq

/**
 * Order-start use case: idempotency lookup, cross-instance forwarding for
 * pending mappings owned elsewhere, input validation, and workflow relay.
 * Never appends domain events itself.
 */
@Component
class StartOrderUseCase(
    private val store: CommerceStore,
    private val workflows: OrderWorkflowRouter,
    private val channels: ZLinkClient,
    private val options: CommerceApiInstanceOptions,
) {
    suspend fun execute(request: StartOrderReq): StartOrderRes {
        val existing = store.findIdempotency(request.idempotencyKey)
        if (existing != null && existing.started) {
            val state = store.findReadModel(existing.orderId) ?: store.placeholder(existing.orderId)
            return StartOrderRes(state.orderId, state.status)
        }
        if (existing != null && existing.ownerInstanceId != options.instanceId) {
            return forwardToOwner(existing.ownerInstanceId, request)
        }

        val cart = store.findCart(request.cartId)
            ?: throw IllegalArgumentException("Unknown cart '${request.cartId}'.")
        if (!store.shippingAddressExists(request.shippingAddressId)) {
            throw IllegalArgumentException("Unknown shipping address '${request.shippingAddressId}'.")
        }
        if (!store.paymentMethodExists(request.paymentMethodId)) {
            throw IllegalArgumentException("Unknown payment method '${request.paymentMethodId}'.")
        }

        val mapping = existing
            ?: store.reserveIdempotency(request.idempotencyKey, options.instanceId)
        store.saveOrderPaymentMethod(mapping.orderId, request.paymentMethodId)

        val command = StartOrderWorkflowReq(
            mapping.orderId,
            request.cartId,
            request.shippingAddressId,
            request.paymentMethodId,
            request.idempotencyKey,
            cart.lines,
            cart.amount,
            cart.currency,
        )
        val state = workflows.startWorkflow(command)
        return StartOrderRes(state.orderId, state.status)
    }

    private suspend fun forwardToOwner(ownerInstanceId: String, request: StartOrderReq): StartOrderRes {
        val channel = SampleNames.commerceApiChannel(ownerInstanceId)
        var lastError: RuntimeException? = null
        for (attempt in 1..SampleTimings.MaxChannelAttempts) {
            try {
                return channels.requestToChannel(channel, request)
                    .timeout(SampleTimings.RequestTimeout)
                    .submit(StartOrderRes::class.java)
                    .await()
            } catch (error: RuntimeException) {
                lastError = error
                LockSupport.parkNanos(SampleTimings.ChannelRetryDelay.toNanos())
            }
        }
        throw IllegalStateException(
            "Peer CommerceApi '$ownerInstanceId' was not ready for forwarded start.", lastError,
        )
    }
}
