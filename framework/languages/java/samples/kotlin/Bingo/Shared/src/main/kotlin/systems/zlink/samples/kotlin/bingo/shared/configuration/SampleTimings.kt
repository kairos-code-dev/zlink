package systems.zlink.samples.kotlin.bingo.shared.configuration

import java.time.Duration

object SampleTimings {
    val ConnectTimeout: Duration = Duration.ofSeconds(5)
    val RequestTimeout: Duration = Duration.ofSeconds(10)
    val DrawPeriod: Duration = Duration.ofMillis(20)
}
