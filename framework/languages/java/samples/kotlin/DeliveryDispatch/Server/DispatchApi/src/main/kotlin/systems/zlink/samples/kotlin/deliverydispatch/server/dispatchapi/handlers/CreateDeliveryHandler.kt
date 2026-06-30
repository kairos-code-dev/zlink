package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi.handlers

import java.util.concurrent.locks.LockSupport
import kotlinx.coroutines.future.await
import org.springframework.web.bind.annotation.GetMapping
import org.springframework.web.bind.annotation.PostMapping
import org.springframework.web.bind.annotation.RequestBody
import org.springframework.web.bind.annotation.RestController
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDeliveryRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CreateDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CreateDeliveryRes

@RestController
class CreateDeliveryHandler(
    private val channels: ZLinkClient,
) {
    @GetMapping("/health")
    fun health(): Map<String, Any> = mapOf("ready" to true, "role" to "dispatch-api")

    @PostMapping("/deliveries")
    suspend fun handle(@RequestBody request: CreateDeliveryReq): CreateDeliveryRes {
        val assign = AssignDeliveryReq(
            request.deliveryId,
            request.customerId,
            request.pickupAddress,
            request.dropoffAddress,
        )
        val assigned = requestDispatch(assign)
        System.err.println(
            "deliverydispatch api: created delivery=${assigned.deliveryId} courier=${assigned.courierId}",
        )
        return CreateDeliveryRes(assigned.deliveryId)
    }

    private suspend fun requestDispatch(request: AssignDeliveryReq): AssignDeliveryRes {
        var lastError: RuntimeException? = null
        for (attempt in 1..SampleTimings.MaxChannelAttempts) {
            try {
                return channels.requestToChannel(SampleNames.DispatchChannel, request)
                    .timeout(SampleTimings.RequestTimeout)
                    .submit(AssignDeliveryRes::class.java)
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
