package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import kotlinx.coroutines.future.await
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
    suspend fun handle(@RequestBody request: CreateGameHttpReq): CreateGameHttpRes {
        val game = client.requestToChannel(
            SampleNames.PlayChannel,
            CreateGameReq(request.gameName?.takeIf { it.isNotBlank() } ?: "tictactoe-game"),
        )
            .timeout(SampleNames.RequestTimeout)
            .submit(CreateGameRes::class.java)
            .await()
        return CreateGameHttpRes(
            roomId = game.roomId,
            gameName = game.gameName,
            ownerPlayEndpoint = game.ownerPlayEndpoint,
            playEndpoints = game.playEndpoints,
            playNodes = game.playNodes,
            requiredLevel = game.requiredLevel,
        )
    }
}
