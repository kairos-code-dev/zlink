package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi.handlers

import java.util.concurrent.locks.LockSupport
import kotlinx.coroutines.future.await
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDeliveryResult
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CreateDeliveryRequest
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryCreated

@ZLinkHandlerGroup("api")
class CreateDeliveryHandler(
    private val channels: ZLinkClient,
) : ZLinkSuspendingRequestHandler<CreateDeliveryRequest, DeliveryCreated> {
    override suspend fun handle(
        request: CreateDeliveryRequest,
        context: ZLinkRequestContext,
    ) = run {
        val assign = AssignDelivery(
            request.deliveryId,
            request.customerId,
            request.pickupAddress,
            request.dropoffAddress,
        )
        val assigned = requestDispatch(assign)
        System.err.println(
            "deliverydispatch api: created delivery=${assigned.deliveryId} courier=${assigned.courierId}",
        )
        DeliveryCreated(assigned.deliveryId)
    }

    private suspend fun requestDispatch(request: AssignDelivery): AssignDeliveryResult {
        var lastError: RuntimeException? = null
        for (attempt in 1..SampleTimings.MaxChannelAttempts) {
            try {
                return channels.requestToChannel(SampleNames.DispatchChannel, request)
                    .timeout(SampleTimings.RequestTimeout)
                    .submit(AssignDeliveryResult::class.java)
                    .await()
            } catch (error: RuntimeException) {
                lastError = error
                LockSupport.parkNanos(SampleTimings.ChannelRetryDelay.toNanos())
            }
        }
        throw IllegalStateException(
            "DispatchCenter route was not ready for delivery '${request.deliveryId}'.",
            lastError,
        )
    }
}
