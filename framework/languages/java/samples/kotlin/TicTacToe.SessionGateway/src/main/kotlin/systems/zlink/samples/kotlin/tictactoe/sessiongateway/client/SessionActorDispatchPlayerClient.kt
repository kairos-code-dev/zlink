package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.kotlin.bind
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSession
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.RecordingSessionActors

class SessionActorDispatchPlayerClient {
    private val actors = RecordingSessionActors()
    private val session = PlayerSession(actors)

    suspend fun bind(actorRef: ZLinkActorRef) {
        session.context().actors().bind(actorRef)
    }

    fun boundActorId(): String = actors.bound()[0].actorId()
}
