package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineSessionPacketHandler
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class PlaceMarkSessionPacketHandler(
    private val channels: ZLinkClient,
    private val relay: PlayNotificationRelay,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSessionPacketHandler<ZLinkSessionContext>(coroutines, "PlaceMarkReq") {
    override suspend fun handle(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        val actor = CreateMatchSessionPacketHandler.requireSingleBoundActor(context)
        val response = channels.requestToChannel(
            SampleNames.PlayChannel,
            "${payload.toUtf8String()}|${actor.actorId()}",
        )
            .packetName("PlaceMarkReq")
            .submitAsync(String::class.java)
            .await()
        relay.deliver(response)
        context.client()
            .reply(relay.reply(response))
            .submitAsync()
            .await()
    }
}
