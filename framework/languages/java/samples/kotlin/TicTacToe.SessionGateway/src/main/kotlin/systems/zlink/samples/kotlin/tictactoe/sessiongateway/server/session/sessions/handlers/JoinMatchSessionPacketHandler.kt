package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class JoinMatchSessionPacketHandler(
    private val channels: ZLinkClient,
) : ZLinkSessionPacketHandler<ZLinkSessionContext> {
    override fun packetName(): String = "JoinMatchReq"

    override fun handleAsync(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ): CompletionStage<Void> {
        val actor = CreateMatchSessionPacketHandler.requireSingleBoundActor(context)
        return channels.requestToChannel(
            SampleNames.PlayChannel,
            "${payload.toUtf8String().trim()}|${actor.actorId()}",
        )
            .packetName("JoinMatchReq")
            .submitAsync(String::class.java)
            .thenCompose { response ->
                PlayNotificationRelay.deliverAsync(response)
                    .thenCompose {
                        context.client()
                            .reply(PlayNotificationRelay.reply(response))
                            .submitAsync()
                    }
            }
    }
}
