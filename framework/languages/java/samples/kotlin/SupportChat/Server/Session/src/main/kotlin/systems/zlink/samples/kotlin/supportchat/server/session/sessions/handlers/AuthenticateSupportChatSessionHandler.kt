package systems.zlink.samples.kotlin.supportchat.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AuthenticateUserReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AuthenticateUserRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorRes

class AuthenticateSupportChatSessionHandler(
    private val channels: ZLinkClient,
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
            .requestToChannel(SampleNames.ApiChannel, AuthenticateUserReq(request.accessToken))
            .timeout(SampleTimings.RequestTimeout)
            .submit(AuthenticateUserRes::class.java)
            .await()
        val actorId = authenticated.actorId
        val displayName = authenticated.displayName
        val role = authenticated.role
        if (!authenticated.accepted ||
            actorId.isNullOrBlank() ||
            displayName.isNullOrBlank() ||
            role.isNullOrBlank()
        ) {
            throw IllegalStateException(
                authenticated.reason ?: "SupportChat authentication failed.",
            )
        }
        val ensured = channels
            .requestToChannel(
                SampleNames.SupportChannel,
                EnsureSupportUserActorReq(actorId, displayName, role),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(EnsureSupportUserActorRes::class.java)
            .await()
        context.actors()
            .bind(
                ZLinkActorRef(
                    RoutingId.from(ensured.actor.nodeRid),
                    ensured.actor.actorId,
                    ensured.actor.generation,
                ),
            )
            .await()
        context.client()
            .reply(AuthenticateRes(actorId, displayName, role))
            .submit()
            .await()
    }
}
