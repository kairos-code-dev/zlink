package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter

import jakarta.annotation.PostConstruct
import jakarta.annotation.PreDestroy
import java.time.Duration
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.locks.LockSupport
import kotlinx.coroutines.future.await
import kotlinx.coroutines.runBlocking
import org.springframework.stereotype.Component
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusAck
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChanged
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatuses
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryResult

@Component
class DispatchWorker(
    private val queue: DispatchWorkQueue,
    private val channels: ZLinkClient,
) {
    private val running = AtomicBoolean(false)
    private var worker: Thread? = null

    @PostConstruct
    fun start() {
        running.set(true)
        worker = Thread({ pump() }, "deliverydispatch-dispatch-worker").apply {
            isDaemon = true
            start()
        }
    }

    @PreDestroy
    fun stop() {
        running.set(false)
        queue.signalStop()
        worker?.interrupt()
    }

    private fun pump() {
        while (running.get()) {
            val request =
                try {
                    queue.take()
                } catch (ignored: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return
            } ?: return
            try {
                runBlocking { dispatch(request) }
            } catch (error: Exception) {
                System.err.println(
                    "deliverydispatch dispatch: failed delivery=${request.deliveryId} error=${error.message}",
                )
            }
        }
    }

    private suspend fun dispatch(request: AssignDelivery) {
        System.err.println(
            "deliverydispatch dispatch: assign delivery=${request.deliveryId} customer=${request.customerId}",
        )
        publishStatus(request.deliveryId, DeliveryStatuses.Assigned, SampleNames.CourierA)

        val first = tryOffer(request, SampleNames.CourierA)
        if (first.accepted) {
            continueAccepted(request.deliveryId, first.courierId)
            return
        }

        publishStatus(request.deliveryId, DeliveryStatuses.Reassigned, SampleNames.CourierB)
        val second = requestWithRetry(
            SampleNames.courierChannel(SampleNames.CourierB),
            OfferDelivery(request.deliveryId, request.pickupAddress, request.dropoffAddress),
            OfferDeliveryResult::class.java,
            null,
            SampleTimings.MaxChannelAttempts,
        )
        if (!second.accepted) {
            publishStatus(request.deliveryId, DeliveryStatuses.Failed, second.courierId)
            throw IllegalStateException("Delivery '${request.deliveryId}' was rejected by all couriers.")
        }
        continueAccepted(request.deliveryId, second.courierId)
    }

    private suspend fun tryOffer(request: AssignDelivery, courierId: String): OfferDeliveryResult =
        try {
            requestWithRetry(
                SampleNames.courierChannel(courierId),
                OfferDelivery(request.deliveryId, request.pickupAddress, request.dropoffAddress),
                OfferDeliveryResult::class.java,
                SampleTimings.DispatchTimeout,
                1,
            )
        } catch (error: Exception) {
            System.err.println(
                "deliverydispatch dispatch: courier timeout delivery=${request.deliveryId} courier=$courierId",
            )
            OfferDeliveryResult(request.deliveryId, courierId, false, error.message)
        }

    private suspend fun continueAccepted(deliveryId: String, courierId: String) {
        publishStatus(deliveryId, DeliveryStatuses.Accepted, courierId)
        publishStatus(deliveryId, DeliveryStatuses.PickedUp, courierId)
        publishStatus(deliveryId, DeliveryStatuses.Delivered, courierId)
    }

    private suspend fun publishStatus(deliveryId: String, status: String, courierId: String?) {
        requestWithRetry(
            SampleNames.TrackingChannel,
            DeliveryStatusChanged(deliveryId, status, courierId, System.currentTimeMillis()),
            DeliveryStatusAck::class.java,
            null,
            SampleTimings.MaxChannelAttempts,
        )
    }

    private suspend fun <TReply> requestWithRetry(
        channelName: String,
        request: Any,
        replyType: Class<TReply>,
        timeout: Duration?,
        maxAttempts: Int,
    ): TReply {
        var lastError: Exception? = null
        for (attempt in 1..maxAttempts) {
            try {
                val call = channels.requestToChannel(channelName, request)
                    .timeout(timeout ?: SampleTimings.RequestTimeout)
                return call.submit(replyType).await()
            } catch (error: Exception) {
                lastError = error
                if (maxAttempts == 1) {
                    throw error
                }
                LockSupport.parkNanos(SampleTimings.ChannelRetryDelay.toNanos())
            }
        }
        throw IllegalStateException("Channel '$channelName' was not ready for request.", lastError)
    }
}
