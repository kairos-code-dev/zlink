package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class SupportUserActor(
    val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    var displayName: String = actorId
        private set

    var role: String = ""
        private set

    var participantId: String = actorId
        private set

    var conversationId: String = ""
        private set

    override fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context

    fun setIdentity(
        displayName: String,
        role: String,
        participantId: String,
    ) {
        this.displayName = displayName
        this.role = role
        this.participantId = participantId
    }

    fun joinConversation(conversationId: String) {
        this.conversationId = conversationId
    }
}
