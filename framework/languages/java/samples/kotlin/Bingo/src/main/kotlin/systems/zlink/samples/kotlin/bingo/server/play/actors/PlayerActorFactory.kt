package systems.zlink.samples.kotlin.bingo.server.play.actors

class PlayerActorFactory {
    fun create(actorId: String): PlayerActor = PlayerActor(actorId)
}
