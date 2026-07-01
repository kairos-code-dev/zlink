package systems.zlink.e2e.kotlin.spotservice.client.support

import java.time.Duration

private val EVENTUAL_TIMEOUT: Duration = Duration.ofSeconds(30)

internal fun <T> eventually(action: () -> T): T {
    val deadline = System.nanoTime() + EVENTUAL_TIMEOUT.toNanos()
    var lastFailure: Exception? = null
    while (System.nanoTime() < deadline) {
        try {
            return action()
        } catch (error: Exception) {
            lastFailure = error
            sleep(200)
        }
    }
    throw IllegalStateException("operation did not succeed before timeout", lastFailure)
}

internal fun sleep(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (error: InterruptedException) {
        Thread.currentThread().interrupt()
        throw IllegalStateException("operation interrupted", error)
    }
}
