package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkFramework
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.kotlin.create

class SessionActorDispatchClient {
    private val inbox = SessionActorNotificationInbox()

    suspend fun verifyFrameworkActorCreation(actorManager: ZLinkActorManager) {
        val created = actorManager.create("player-1", "player")
        require(created.actorId() == "player-1") {
            "framework actor manager did not create expected actor"
        }
    }

    suspend fun runReconnectScenario(
        framework: ZLinkFramework,
        options: SessionActorDispatchClientOptions,
    ) {
        val actorRef = ZLinkActorRef(RoutingId.from("play-node"), options.actorId, 1)
        val primary = SessionActorDispatchPlayerClient(framework, RoutingId.from("session-primary"))
        primary.bind(actorRef)

        val reconnect = SessionActorDispatchPlayerClient(framework, RoutingId.from("session-reconnect"))
        reconnect.bind(actorRef)

        require(primary.boundActorId() == options.actorId) {
            "primary session did not bind expected actor"
        }
        require(reconnect.boundActorId() == options.actorId) {
            "reconnect session did not keep actor id"
        }

        inbox.add("bound:${reconnect.boundActorId()}")
        require("bound:${options.actorId}" in inbox.events()) {
            "session actor binding did not reach client-facing session"
        }
    }
}
