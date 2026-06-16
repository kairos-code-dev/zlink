package systems.zlink.samples.kotlin.deliverydispatch.server.courier.handlers

import java.time.Duration
import java.util.Locale
import java.util.concurrent.locks.LockSupport
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.courier.CourierOptions
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryResult

class OfferDeliveryHandler(
    private val options: CourierOptions,
) : ZLinkRequestHandler<OfferDelivery, OfferDeliveryResult> {
    override fun handle(
        request: OfferDelivery,
        context: ZLinkRequestContext,
    ): OfferDeliveryResult {
        if (shouldTimeout(request.deliveryId)) {
            System.err.println(
                "deliverydispatch courier: no response delivery=${request.deliveryId} courier=${options.courierId}",
            )
            LockSupport.parkNanos(Duration.ofSeconds(30).toNanos())
        }
        val accepted = !options.mode.equals("reject", ignoreCase = true)
        System.err.println(
            "deliverydispatch courier: decision delivery=${request.deliveryId} courier=${options.courierId} accepted=$accepted",
        )
        return OfferDeliveryResult(
            request.deliveryId,
            options.courierId,
            accepted,
            if (accepted) null else "courier rejected",
        )
    }

    private fun shouldTimeout(deliveryId: String): Boolean {
        val mode = options.mode.lowercase(Locale.ROOT)
        if (mode == "timeout") {
            return true
        }
        return mode == "timeout-reassign" && deliveryId.lowercase(Locale.ROOT).contains("reassign")
    }
}
