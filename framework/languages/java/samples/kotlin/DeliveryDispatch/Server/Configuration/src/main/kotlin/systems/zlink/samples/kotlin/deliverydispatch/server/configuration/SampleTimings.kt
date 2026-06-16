package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

import java.time.Duration

object SampleTimings {
    val FrameworkTimeout: Duration = Duration.ofSeconds(5)
    val DispatchTimeout: Duration = Duration.ofMillis(700)
    val RequestTimeout: Duration = Duration.ofSeconds(10)
    val ChannelRetryDelay: Duration = Duration.ofMillis(100)
    const val MaxChannelAttempts = 40
}
