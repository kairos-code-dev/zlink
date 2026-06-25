package systems.zlink.e2e.kotlin.registrymessaging

object Contracts {
    const val API_CHANNEL = "registry.messaging.kotlin.api"
    const val WORKFLOW_CHANNEL = "registry.messaging.kotlin.workflow"
    const val ROUTE_CHANNEL = "registry.messaging.kotlin.route"
    const val HANDLER_GROUP = "registry-messaging-kotlin"
    const val ROUTE_PACKET = "KotlinScenarioRoutePing"
}

class ProfileRequest() {
    var value: String = ""

    constructor(value: String) : this() {
        this.value = value
    }
}

class ProfileReply() {
    var value: String = ""
    var providerRid: String = ""
    var instanceId: String = ""

    constructor(
        value: String,
        providerRid: String,
        instanceId: String,
    ) : this() {
        this.value = value
        this.providerRid = providerRid
        this.instanceId = instanceId
    }
}

class ProfileCommand() {
    var commandId: String = ""

    constructor(commandId: String) : this() {
        this.commandId = commandId
    }
}

class RoutePing() {
    var value: String = ""

    constructor(value: String) : this() {
        this.value = value
    }
}

class RoutePong() {
    var value: String = ""
    var targetRid: String = ""
    var sourceRid: String = ""

    constructor(
        value: String,
        targetRid: String,
        sourceRid: String,
    ) : this() {
        this.value = value
        this.targetRid = targetRid
        this.sourceRid = sourceRid
    }
}

data class EvidenceEntry(
    val marker: String,
    val providerRid: String,
    val value: String,
)

data class EvidenceSnapshot(
    val providerRid: String,
    val entries: List<EvidenceEntry>,
)
