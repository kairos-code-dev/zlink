package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors

class SupportActorDirectory {
    private val gate = Any()
    private val actors = mutableMapOf<String, SupportUserActor>()

    fun addOrUpdate(actor: SupportUserActor) {
        synchronized(gate) {
            actors[actor.actorId()] = actor
        }
    }

    fun get(actorId: String): SupportUserActor =
        synchronized(gate) {
            actors[actorId]
                ?: throw IllegalStateException("Support actor is not available. actor=$actorId")
        }
}
