package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import java.util.concurrent.CompletionStage
import org.springframework.web.bind.annotation.PostMapping
import org.springframework.web.bind.annotation.RequestBody
import org.springframework.web.bind.annotation.RestController
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

@RestController
class CreateGameHttpHandler(
    private val client: ZLinkClient,
) {
    @PostMapping("/games")
    fun handleAsync(@RequestBody request: CreateGameHttpReq): CompletionStage<CreateGameHttpRes> =
        client.requestToChannel(
            SampleNames.PlayChannel,
            CreateGameReq(request.gameName?.takeIf { it.isNotBlank() } ?: "tictactoe-game"),
        )
            .packetName("CreateGameReq")
            .submitAsync(CreateGameRes::class.java)
            .thenApply { game -> CreateGameHttpRes(game.gameId, game.playEndpoint, game.gameName) }
}
