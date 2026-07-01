package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

import java.time.Duration

object SampleTimings {
    val RequestTimeout: Duration = Duration.ofSeconds(10)
    val OfferRequestTimeout: Duration = Duration.ofSeconds(2)
    val CourierDecisionTimeout: Duration = Duration.ofMillis(900)
}
