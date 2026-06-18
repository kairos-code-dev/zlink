package systems.zlink.samples.kotlin.gamequest.client.configuration

import java.time.Duration

object SampleTimings {
    val ConnectTimeout: Duration = Duration.ofSeconds(10)
    val RequestTimeout: Duration = Duration.ofSeconds(10)
    val AssertAttemptTimeout: Duration = Duration.ofSeconds(2)
    val PollDelay: Duration = Duration.ofMillis(100)
    val NotifyTimeout: Duration = Duration.ofSeconds(20)
}
