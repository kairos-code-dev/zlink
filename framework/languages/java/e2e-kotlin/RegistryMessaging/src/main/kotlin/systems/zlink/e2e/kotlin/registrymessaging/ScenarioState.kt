package systems.zlink.e2e.kotlin.registrymessaging

class ScenarioState(
    val providerRid: String,
    val instanceId: String,
) {
    private val entries = mutableListOf<EvidenceEntry>()

    @Synchronized
    fun record(marker: String, value: String) {
        entries += EvidenceEntry(marker, providerRid, value)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(providerRid, entries.toList())
}
