package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler

class RemoteActorAuthHandler(
    private val routes: ZLinkRouteClient,
    private val evidence: ScenarioState
) : ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ActorRemoteAuthReq> {
    override fun packetName(): String = "ActorRemoteAuthReq"

    override fun messageType(): Class<Contracts.ActorRemoteAuthReq> = Contracts.ActorRemoteAuthReq::class.java

    override fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: Contracts.ActorRemoteAuthReq
    ) {
        val ensured = routes.requestToNode(
            Contracts.ROUTE_CHANNEL,
            RoutingId.from(request.nodeRid),
            Contracts.EnsureActorReq(request.actorId, request.profile)
        )
            .packetName("EnsureActorReq")
            .await(Contracts.EnsureActorRes::class.java)
        context.actors()
            .bind(ActorRef(RoutingId.from(ensured.nodeRid), ensured.actorId, ensured.generation))
            .whenComplete { bound, error ->
                if (error != null) {
                    evidence.record("RemoteActorSessionBindFailed", "session", error.javaClass.simpleName)
                    return@whenComplete
                }
                evidence.record("RemoteActorSessionBound", "session", request.actorId + "/" + ensured.nodeRid)
                context.client()
                    .reply(
                        Contracts.ActorAuthRes(
                            bound.actorId(),
                            bound.ref().nodeRid().toString(),
                            context.actors().bound().size,
                            request.profile.displayName,
                            request.profile.level,
                            request.profile.tags
                        )
                    )
                    .await()
            }
    }
}
