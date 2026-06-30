package systems.zlink.e2e.kotlin.discoveryregistryha

class Contracts private constructor() {
    companion object {
        const val CHANNEL: String = "discovery.registry.ha.api"
        const val HANDLER_GROUP: String = "discovery-registry-ha"
    }

    @JvmRecord
    data class WorkRequest(val value: String)

    @JvmRecord
    data class WorkReply(
        val value: String,
        val providerRid: String,
    )
}
