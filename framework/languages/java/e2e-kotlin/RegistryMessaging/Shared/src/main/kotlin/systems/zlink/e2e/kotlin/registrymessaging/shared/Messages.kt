package systems.zlink.e2e.kotlin.registrymessaging.shared

object Contracts {
    const val PROFILE_CHANNEL = "profile"
    const val PROFILE_MANUAL_CHANNEL = "profile.manual"
    const val PROFILE_ROUTE_CHANNEL = "profile.route"
    const val WORKFLOW_CHANNEL = "workflow"
    const val PROFILE_REQUEST_PACKET = "ProfileRequest"
    const val PROFILE_COMMAND_PACKET = "ProfileCommand"
    const val PAYLOAD_REQUEST_PACKET = "PayloadRequest"
    const val ROUTE_PACKET = "ScenarioRoutePing"
    const val WORKFLOW_REQUEST_PACKET = "WorkflowRequest"
}

data class ProfileRequest(
    var value: String = "",
)

data class ProfileReply(
    var value: String = "",
    var providerRid: String = "",
)

data class ProfileCommand(
    var commandId: String = "",
)

data class EvidenceWaitRequest(
    var contains: String = "",
    var timeoutMilliseconds: Int = 10000,
)

data class PayloadRequest(
    var marker: String = "",
    var payload: String = "",
)

data class PayloadReply(
    var marker: String = "",
    var length: Int = 0,
    var sha256: String = "",
)

data class WorkflowRequest(
    var value: String = "",
)

data class WorkflowReply(
    var value: String = "",
    var providerRid: String = "",
)

data class ScenarioRoutePing(
    var value: String = "",
)

data class ScenarioRoutePong(
    var value: String = "",
    var providerRid: String = "",
    var sourceRid: String = "",
)

data class RouteMissingResult(
    var failed: Boolean = false,
)

data class RequestFailureResult(
    var failed: Boolean = false,
    var failureType: String = "",
)
