package systems.zlink.samples.kotlin.deliverydispatch.client.configuration

import java.time.Duration

object SampleTimings {
    val RequestTimeout: Duration = Duration.ofSeconds(12)
    val ConnectTimeout: Duration = Duration.ofSeconds(5)
    val NotifyTimeout: Duration = Duration.ofSeconds(8)
}
