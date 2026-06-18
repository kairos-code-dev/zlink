package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineTypedSessionPacketHandler
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes

class AuthenticatePlaySessionHandler(
    private val actors: ZLinkActorManager,
    private val channels: ZLinkClient,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineTypedSessionPacketHandler<ZLinkSessionContext, AuthenticateReq>(
    coroutines,
    "AuthenticateReq",
    AuthenticateReq::class.java,
) {
    override suspend fun handleSuspending(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        request: AuthenticateReq,
    ) {
        require(request.accessToken.isNotBlank()) { "access token is required" }
        val authenticated = channels
            .requestToChannel(SampleNames.ApiChannel, AuthenticatePlayerReq(request.accessToken))
            .timeout(SampleNames.RequestTimeout)
            .submit(AuthenticatePlayerRes::class.java)
            .await()
        val playActor = actors.getOrCreate(authenticated.actorId, SampleNames.PlayActor).await()
        val bound = context.actors().bind(playActor).await()
        context.client()
            .reply(AuthenticateRes(bound.actorId()))
            .submit()
            .await()
    }
}
