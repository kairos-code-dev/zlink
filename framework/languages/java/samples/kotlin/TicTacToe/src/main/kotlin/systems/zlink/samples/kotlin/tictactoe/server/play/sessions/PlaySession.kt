package systems.zlink.samples.kotlin.tictactoe.server.play.sessions

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameState

class PlaySession(
    private val context: ZLinkSessionContext,
) : ZLinkSession {
    override fun context(): ZLinkSessionContext = context
    override fun onConnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
    override fun onDisconnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
    override fun onErrorAsync(error: ZLinkStreamError): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)

    override fun onDispatchAsync(header: ZLinkStreamHeader, payload: Message): CompletionStage<Void> =
        try {
            val body = payload.toUtf8String()
            when (header.packetName()) {
                "AuthenticateReq" -> context.client().reply(body.trim()).submitAsync()
                "JoinGameReq" -> {
                    val parts = body.split("|", limit = 2)
                    val state = TicTacToeGameDirectory.get(parts[0]).join(parts[1]).state
                    context.client().reply(reply(state)).submitAsync()
                }
                "PlaceMarkReq" -> {
                    val parts = body.split("|", limit = 3)
                    val game = TicTacToeGameDirectory.get(parts[0])
                    val state = game.placeMark(parts[1], parts[2].toInt()).state
                    context.client().reply(reply(state.winner, game.pushes)).submitAsync()
                }
                else -> CompletableFuture.failedFuture(
                    IllegalArgumentException("unsupported stream packet: ${header.packetName()}"),
                )
            }
        } catch (ex: RuntimeException) {
            CompletableFuture.failedFuture(ex)
        }

    private fun reply(state: GameState): String = reply(state.winner, emptyList())

    private fun reply(winner: String?, pushes: List<String>): String =
        "${winner.orEmpty()}|${pushes.joinToString(",")}"
}
