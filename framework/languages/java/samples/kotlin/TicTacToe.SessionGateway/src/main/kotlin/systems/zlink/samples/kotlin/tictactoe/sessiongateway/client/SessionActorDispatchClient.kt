package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.kotlin.create
import systems.zlink.framework.kotlin.submit
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActor

class SessionActorDispatchClient {
    private val inbox = SessionActorNotificationInbox()

    suspend fun verifyFrameworkActorCreation(actorManager: ZLinkActorManager) {
        val created = actorManager.create("player-1", "player")
        require(created.actorId() == "player-1") {
            "framework actor manager did not create expected actor"
        }
    }

    suspend fun runReconnectScenario(options: SessionActorDispatchClientOptions) {
        val actorRef = ZLinkActorRef(RoutingId.from("play-node"), options.actorId, 1)
        val primary = SessionActorDispatchPlayerClient()
        primary.bind(actorRef)

        val reconnect = SessionActorDispatchPlayerClient()
        reconnect.bind(actorRef)

        require(primary.boundActorId() == options.actorId) {
            "primary session did not bind expected actor"
        }
        require(reconnect.boundActorId() == options.actorId) {
            "reconnect session did not keep actor id"
        }

        val actor = PlayerActor(options.actorId)
        actor.context().boundSession()
            .send("GameState")
            .packetName("GameStateChanged")
            .submit()
        inbox.addAll(actor.pushes())
        require("GameStateChanged:GameState" in inbox.events()) {
            "boundSession push did not reach client-facing session"
        }
    }
}
