package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class SupportUserActor(
    private val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    var displayName: String = actorId
        private set
    var role: String = ""
        private set
    var conversationId: String = ""
        private set
    var disconnected: Boolean = false
        private set

    override fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context

    fun setIdentity(displayName: String, role: String) {
        this.displayName = displayName
        this.role = role
    }

    fun joinConversation(value: String) {
        conversationId = value
    }

    fun markDisconnected() {
        disconnected = true
    }
}
