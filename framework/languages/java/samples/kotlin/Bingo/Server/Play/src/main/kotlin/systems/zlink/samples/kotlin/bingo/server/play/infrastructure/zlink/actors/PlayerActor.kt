package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class PlayerActor(
    private val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    var displayName: String = actorId
        private set
    var roomId: String = ""
        private set
    var destroyAfterEntrySpotJoin: Boolean = false
        private set
    var disconnected: Boolean = false
        private set

    fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context

    fun setDisplayName(value: String) {
        displayName = value
    }

    fun joinRoom(value: String) {
        roomId = value
    }

    fun markForDestroyAfterRoomLeave() {
        destroyAfterEntrySpotJoin = true
    }

    fun markDisconnected() {
        disconnected = true
    }

    fun push(message: Any) {
        context.boundSession()
            .send(message)
            .submit()
    }
}
