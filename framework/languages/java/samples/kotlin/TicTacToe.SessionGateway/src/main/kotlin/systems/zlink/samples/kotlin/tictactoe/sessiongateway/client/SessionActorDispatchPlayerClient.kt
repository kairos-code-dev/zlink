package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkFramework
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.kotlin.bind
import systems.zlink.framework.streams.ZLinkSessionActors
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class SessionActorDispatchPlayerClient(
    framework: ZLinkFramework,
    sessionRid: RoutingId,
) {
    private val actors: ZLinkSessionActors =
        framework.sessionActors(SampleNames.GatewayStream, sessionRid)

    suspend fun bind(actorRef: ZLinkActorRef) {
        actors.bind(actorRef)
    }

    fun boundActorId(): String = actors.bound()[0].actorId()
}
