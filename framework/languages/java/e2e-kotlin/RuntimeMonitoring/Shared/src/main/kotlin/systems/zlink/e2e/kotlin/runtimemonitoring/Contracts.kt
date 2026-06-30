package systems.zlink.e2e.kotlin.runtimemonitoring

class Contracts private constructor() {
    companion object {
        const val CHANNEL: String = "monitoring.api"
        const val HANDSHAKE_CHANNEL: String = "monitoring.handshake"
        const val SPOT_MESH: String = "monitoring.spot.mesh"
        const val HANDLER_GROUP: String = "monitoring"
        const val REGISTRY_SOURCE: String = "ops-registry"
    }

    @JvmRecord
    data class WorkRequest(val value: String)

    @JvmRecord
    data class WorkReply(
        val value: String,
        val providerRid: String,
    )

    @JvmRecord
    data class EvidenceEntry(
        val surface: String,
        val sourceName: String,
        val event: String,
        val detail: String,
    )

    @JvmRecord
    data class EvidenceSnapshot(val entries: List<EvidenceEntry>)
}
