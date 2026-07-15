package systems.zlink.samples.kotlin.bingo.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler
import systems.zlink.framework.spots.SpotHandleResolver
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

class AuthenticateSessionHandler(
    private val channels: ZLinkClient,
    private val routes: ZLinkRouteClient,
    private val spots: SpotHandleResolver,
    private val topology: SampleTopology,
) : ZLinkSuspendingTypedSessionPacketHandler<ZLinkSessionContext, AuthenticateReq> {
    override fun packetName(): String = "AuthenticateReq"

    override fun messageType(): Class<AuthenticateReq> = AuthenticateReq::class.java

    override suspend fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: AuthenticateReq,
    ) {
        if (request.accessToken.isBlank()) {
            throw IllegalArgumentException("access token is required")
        }

        val authenticated = channels
            .requestToChannel(SampleNames.ApiChannel, AuthenticatePlayerReq(request.accessToken))
            .timeout(SampleTimings.RequestTimeout)
            .submit(AuthenticatePlayerRes::class.java)
            .await()
        if (!authenticated.accepted ||
            authenticated.actorId.isBlank() ||
            authenticated.displayName.isBlank()
        ) {
            throw IllegalStateException(
                authenticated.reason ?: "Player authentication failed.",
            )
        }
        val preferredPlayNode = RoutingId.from(topology.preferredPlayNodeRid())
        val spot = spots.resolveSpotHandle(preferredPlayNode).await()
            .orElseThrow { IllegalStateException("spot not found: $preferredPlayNode") }
        val ensured = routes
            .requestToSpot(
                SampleNames.RoomSpotDiscovery,
                spot,
                EnsurePlayerActorReq(
                    authenticated.actorId,
                    authenticated.displayName,
                    topology.preferredPlayNodeRid(),
                ),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(EnsurePlayerActorRes::class.java)
            .await()
        context.actors()
            .bind(
                ActorRef(
                    RoutingId.from(ensured.actor.nodeRid),
                    ensured.actor.actorId,
                    ensured.actor.generation,
                ),
            )
            .await()
        context.client()
            .reply(
                AuthenticateRes(
                    ensured.actorId,
                    authenticated.displayName,
                    ensured.actor.nodeRid,
                ),
            )
            .submit()
    }
}
