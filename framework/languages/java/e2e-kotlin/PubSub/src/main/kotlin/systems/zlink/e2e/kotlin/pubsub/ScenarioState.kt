package systems.zlink.e2e.kotlin.pubsub

class ScenarioState(
    val subscriberRid: String,
    private val topics: Set<String>,
) {
    private val entries = mutableListOf<EvidenceEntry>()

    fun accepts(topic: String): Boolean = topics.contains(topic)

    fun delayIfConfigured(scenario: String) {
        val delay = Env.get("ZLINK_KOTLIN_E2E_HANDLER_DELAY_MS")
        if (delay.isBlank() || scenario != "ps-b1") {
            return
        }
        try {
            Thread.sleep(delay.toLong())
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

    companion object {
        fun topicsFromEnv(): Set<String> {
            val values = mutableSetOf<String>()
            Env.get("ZLINK_KOTLIN_E2E_TOPICS", "all").split(",").forEach { part ->
                val trimmed = part.trim()
                if (trimmed.isNotEmpty()) {
                    values += trimmed
                }
            }
            values += "all"
            return values
        }
    }
}
