package systems.zlink.e2e.kotlin.registrymessaging.registry.Infrastructure

class EvidenceStore {
    private val entries = mutableListOf<String>()

    @Synchronized
    fun add(entry: String) {
        entries += entry
    }

    @Synchronized
    fun snapshot(): List<String> = entries.toList()

    @Synchronized
    fun clear() {
        entries.clear()
    }
}
