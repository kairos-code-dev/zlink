package systems.zlink.e2e.kotlin.resiliencelifecycle

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class ScenarioState(private val providerRid: String) {
    private val entries = mutableListOf<Contracts.EvidenceEntry>()
    private val slowStarted = CountDownLatch(1)
    private val slowRelease = CountDownLatch(1)
    private var weight = 100
    private var grayFailure = false

    fun providerRid(): String = providerRid

    @Synchronized
    fun weight(value: Int) {
        weight = value
    }

    @Synchronized
    fun weight(): Int = weight

    @Synchronized
    fun grayFailure(value: Boolean) {
        grayFailure = value
    }

    @Synchronized
    fun grayFailure(): Boolean = grayFailure

    fun releaseSlow() {
        slowRelease.countDown()
    }

    fun awaitSlowRelease() {
        slowStarted.countDown()
        try {
            if (!slowRelease.await(20, TimeUnit.SECONDS)) {
                throw IllegalStateException("slow request was not released")
            }
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted", error)
        }
    }

    @Synchronized
    fun record(marker: String, value: String) {
        entries.add(Contracts.EvidenceEntry(marker, providerRid, value))
    }

    @Synchronized
    fun snapshot(): Contracts.EvidenceSnapshot =
        Contracts.EvidenceSnapshot(providerRid, weight, entries.toList())
}
