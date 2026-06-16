package systems.zlink.samples.kotlin.supportchat.server.configuration

object SampleNames {
    const val ApiChannel: String = "supportchat.api"
    const val SupportChannel: String = "supportchat.support"
    const val StreamNode: String = "supportchat.client.stream"
    const val SessionSpotNode: String = "supportchat.session.node"
    const val SupportActorType: String = "supportchat.user"
    const val SupportEntrySpotNode: String = "supportchat.entry.node"
    const val ConversationSpotNode: String = "supportchat.conversation.node"
    const val SupportSpotDiscovery: String = "supportchat.support.spots"
    const val SupportRouteChannel: String = "supportchat.support.route"
}

object SampleTimings {
    val RequestTimeout: java.time.Duration = java.time.Duration.ofSeconds(10)
    val IdleTimeout: java.time.Duration = java.time.Duration.ofSeconds(3)
    val CloseGraceTimeout: java.time.Duration = java.time.Duration.ofSeconds(2)
    val IdleCheckPeriod: java.time.Duration = java.time.Duration.ofMillis(200)
    const val MaxMessageLength: Int = 500
}

object SupportChatRoles {
    const val Customer: String = "Customer"
    const val Agent: String = "Agent"
}

object ConversationStatuses {
    const val WaitingForAgent: String = "WaitingForAgent"
    const val Active: String = "Active"
    const val WaitingForClose: String = "WaitingForClose"
    const val Closed: String = "Closed"
}
