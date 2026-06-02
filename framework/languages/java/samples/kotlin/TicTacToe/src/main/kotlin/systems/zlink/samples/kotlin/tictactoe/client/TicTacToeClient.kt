package systems.zlink.samples.kotlin.tictactoe.client

import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

class TicTacToeClient(
    private val frameworkClient: ZLinkClient,
) {
    suspend fun run(options: TicTacToeClientOptions): TicTacToeClientResult {
        val host = authenticate(options.hostAccessToken)
        val guest = authenticate(options.guestAccessToken)
        val game = createGame(options.gameName)
        val room = TicTacToeGameDirectory.get(game.gameId)

        var state = room.join(host.actorId).state
        state = room.join(guest.actorId).state
        state = room.placeMark(host.actorId, 0).state
        state = room.placeMark(guest.actorId, 4).state
        state = room.placeMark(host.actorId, 1).state
        state = room.placeMark(guest.actorId, 8).state
        state = room.placeMark(host.actorId, 2).state

        return TicTacToeClientResult(state.winner, room.pushes)
    }

    private suspend fun authenticate(accessToken: String): AuthenticateRes =
        AuthenticateRes(
            frameworkClient
                .requestToChannel("tictactoe-api", accessToken)
                .packetName("AuthenticatePlayer")
                .awaitReply<String>(),
        )

    private suspend fun createGame(gameName: String): CreateGameRes {
        val gameId = frameworkClient
            .requestToChannel("tictactoe-api", gameName)
            .packetName("CreateGame")
            .awaitReply<String>()
        return CreateGameRes(gameId, "inproc://zlink-kotlin-sample-tictactoe-stream", gameName)
    }
}
