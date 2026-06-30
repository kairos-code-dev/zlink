package systems.zlink.e2e.kotlin.pubsub.subscriber

import systems.zlink.e2e.kotlin.pubsub.shared.EvidenceEntry
import systems.zlink.e2e.kotlin.pubsub.shared.EvidenceSnapshot

class EvidenceStore(
    val subscriberRid: String,
    private val topics: Set<String>,
    private val handlerDelayMillis: Long?,
) {
    private val entries = mutableListOf<EvidenceEntry>()

    fun accepts(topic: String): Boolean = topics.contains(topic)

    fun delayIfConfigured(scenario: String) {
        val delay = handlerDelayMillis
        if (delay == null || scenario != "ps-b1") {
            return
        }
        try {
            Thread.sleep(delay)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("subscriber delay interrupted", error)
        }
    }

    @Synchronized
    fun record(
        marker: String,
        topic: String,
        scenario: String,
        sequence: Int,
        value: String,
    ) {
        entries += EvidenceEntry(marker, subscriberRid, topic, scenario, sequence, value)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(subscriberRid, entries.toList())
}
