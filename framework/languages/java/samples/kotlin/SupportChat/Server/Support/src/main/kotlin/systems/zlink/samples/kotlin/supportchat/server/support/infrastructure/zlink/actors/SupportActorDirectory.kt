package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors

import systems.zlink.framework.actors.ZLinkActorRef

class SupportActorDirectory {
    data class Entry(val ref: ZLinkActorRef, val displayName: String, val role: String)

    private val gate = Any()
    private val actors = mutableMapOf<String, Entry>()

    fun addOrUpdate(ref: ZLinkActorRef, displayName: String, role: String) {
        synchronized(gate) {
            actors[ref.actorId()] = Entry(ref, displayName, role)
        }
    }

    fun get(actorId: String): Entry =
        synchronized(gate) {
            actors[actorId]
                ?: throw IllegalStateException("Support actor is not available. actor=$actorId")
        }
}
