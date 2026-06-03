package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

@ZLinkHandlerGroup("api")
class CreateGameHttpHandler(
    private val client: ZLinkClient,
) {
    @ZLinkRequest(packetName = "CreateGame")
    fun handleAsync(request: CreateGameReq): CompletionStage<CreateGameRes> =
        client.requestToChannel(SampleNames.PlayChannel, request)
            .packetName("CreateGameReq")
            .submitAsync(CreateGameRes::class.java)
}
