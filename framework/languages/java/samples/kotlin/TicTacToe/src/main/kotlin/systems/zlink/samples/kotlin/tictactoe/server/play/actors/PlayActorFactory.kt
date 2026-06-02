package systems.zlink.samples.kotlin.tictactoe.server.play.actors

class PlayActorFactory {
    fun create(actorId: String): PlayActor = PlayActor(actorId)
}
