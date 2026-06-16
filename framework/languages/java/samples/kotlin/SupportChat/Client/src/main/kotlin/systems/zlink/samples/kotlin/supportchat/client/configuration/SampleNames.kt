package systems.zlink.samples.kotlin.supportchat.client.configuration

import java.time.Duration

object SampleNames {
    const val ParticipantJoinedPacket: String = "ParticipantJoinedNotify"
    const val ConversationAssignedPacket: String = "ConversationAssignedNotify"
    const val ChatMessagePacket: String = "ChatMessageNotify"
    const val TypingChangedPacket: String = "TypingChangedNotify"
    const val ConversationIdlePacket: String = "ConversationIdleNotify"
    const val ConversationClosedPacket: String = "ConversationClosedNotify"

    const val CustomerRole: String = "Customer"
    const val AgentRole: String = "Agent"

    const val WaitingForAgent: String = "WaitingForAgent"
    const val Active: String = "Active"
    const val WaitingForClose: String = "WaitingForClose"
    const val Closed: String = "Closed"

    val RequestTimeout: Duration = Duration.ofSeconds(10)
    val IdleTimeout: Duration = Duration.ofSeconds(3)
    val CloseGraceTimeout: Duration = Duration.ofSeconds(2)
}

object SampleTimings {
    val ConnectTimeout: Duration = Duration.ofSeconds(5)
    val RequestTimeout: Duration = Duration.ofSeconds(10)
}
