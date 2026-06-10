package systems.zlink.samples.kotlin.tictactoe.client

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import java.time.Duration
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.future.await
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.kotlin
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameStateNotify
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerJoinedNotify
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.msgpack.ZLinkStreamMessagePack

class TicTacToeClient {
    private val http = HttpClient.newHttpClient()
    private val json = ObjectMapper().registerKotlinModule()

    suspend fun run(options: TicTacToeClientOptions) = coroutineScope {
        val game = createGame(options.apiUrl, options.gameName)
        val hostStream = playerConnector(game.playEndpoint)
        val guestStream = playerConnector(game.playEndpoint)

        try {
            hostStream.connect().await()
            guestStream.connect().await()

            ensure(game.roomId.isNotBlank())
            ensure(game.playEndpoint.isNotBlank())
            ensure(game.gameName == options.gameName)

            val xAuthentication = hostStream.request(AuthenticateReq(options.xActorId)).await<AuthenticateRes>()
            ensure(xAuthentication.actorId == options.xActorId)

            val oAuthentication = guestStream.request(AuthenticateReq(options.oActorId)).await<AuthenticateRes>()
            ensure(oAuthentication.actorId == options.oActorId)
            ensure(oAuthentication.actorId != xAuthentication.actorId)

            val xJoin = hostStream.request(JoinGameReq(game.roomId)).await<JoinGameRes>()
            ensure(xJoin.state.roomId == game.roomId)
            ensure(xJoin.state.status == "WaitingForPlayers")
            ensure(xJoin.state.xActorId == options.xActorId)
            ensure(hostStream.receivedCount("PlayerJoinedNotify") == 0)

            val hostSawGuestJoin = hostStream.waitFor<PlayerJoinedNotify>()
                .where { message -> message.payload().actorId == options.oActorId }
                .let { wait -> async { wait.await() } }
            val hostSawGameStart = hostStream.waitFor<GameStateNotify>()
                .where { message -> message.payload().state.status == "InProgress" }
                .let { wait -> async { wait.await() } }

            val oJoin = guestStream.request(JoinGameReq(game.roomId)).await<JoinGameRes>()
            ensure(oJoin.state.roomId == game.roomId)
            ensure(oJoin.state.status == "InProgress")
            ensure(oJoin.state.oActorId == options.oActorId)

            val guestJoinNotify = hostSawGuestJoin.await().payload()
            ensure(guestJoinNotify.actorId == options.oActorId)
            ensure(guestJoinNotify.mark == "O")
            ensure(guestJoinNotify.state.status == "InProgress")
            ensure(guestStream.receivedCount("PlayerJoinedNotify") == 0)

            val gameStarted = hostSawGameStart.await().payload()
            ensure(gameStarted.state.nextTurn == "X")

            val guestSawHostMove1 = guestStream.waitFor<GameStateNotify>()
                .where { message -> message.payload().state.lastMoveCell == 0 }
                .let { wait -> async { wait.await() } }
            val hostMove1 = hostStream.request(PlaceMarkReq(0)).await<PlaceMarkRes>()
            ensure(hostMove1.state.board == "X........")
            ensure(hostMove1.state.nextTurn == "O")
            ensure(hostMove1.state.lastMoveActorId == options.xActorId)
            ensure(hostMove1.state.lastMoveCell == 0)

            val hostMove1Notify = guestSawHostMove1.await().payload()
            ensure(hostMove1Notify.state.board == hostMove1.state.board)
            ensure(hostMove1Notify.state.nextTurn == "O")
            ensure(hostMove1Notify.state.lastMoveActorId == options.xActorId)
            ensure(hostMove1Notify.state.lastMoveCell == 0)

            val hostSawGuestMove1 = hostStream.waitFor<GameStateNotify>()
                .where { message -> message.payload().state.lastMoveCell == 3 }
                .let { wait -> async { wait.await() } }

            val guestMove1 = guestStream.request(PlaceMarkReq(3)).await<PlaceMarkRes>()
            ensure(guestMove1.state.board == "X..O.....")
            ensure(guestMove1.state.nextTurn == "X")
            ensure(guestMove1.state.lastMoveActorId == options.oActorId)
            ensure(guestMove1.state.lastMoveCell == 3)

            val guestMove1Notify = hostSawGuestMove1.await().payload()
            ensure(guestMove1Notify.state.board == guestMove1.state.board)
            ensure(guestMove1Notify.state.nextTurn == "X")
            ensure(guestMove1Notify.state.lastMoveActorId == options.oActorId)
            ensure(guestMove1Notify.state.lastMoveCell == 3)

            val guestSawHostMove2 = guestStream.waitFor<GameStateNotify>()
                .where { message -> message.payload().state.lastMoveCell == 1 }
                .let { wait -> async { wait.await() } }
            val hostMove2 = hostStream.request(PlaceMarkReq(1)).await<PlaceMarkRes>()
            ensure(hostMove2.state.board == "XX.O.....")
            ensure(hostMove2.state.nextTurn == "O")
            ensure(hostMove2.state.lastMoveActorId == options.xActorId)
            ensure(hostMove2.state.lastMoveCell == 1)

            val hostMove2Notify = guestSawHostMove2.await().payload()
            ensure(hostMove2Notify.state.board == hostMove2.state.board)
            ensure(hostMove2Notify.state.nextTurn == "O")
            ensure(hostMove2Notify.state.lastMoveActorId == options.xActorId)
            ensure(hostMove2Notify.state.lastMoveCell == 1)

            val hostSawGuestMove2 = hostStream.waitFor<GameStateNotify>()
                .where { message -> message.payload().state.lastMoveCell == 4 }
                .let { wait -> async { wait.await() } }
            val guestMove2 = guestStream.request(PlaceMarkReq(4)).await<PlaceMarkRes>()
            ensure(guestMove2.state.board == "XX.OO....")
            ensure(guestMove2.state.nextTurn == "X")
            ensure(guestMove2.state.lastMoveActorId == options.oActorId)
            ensure(guestMove2.state.lastMoveCell == 4)

            val guestMove2Notify = hostSawGuestMove2.await().payload()
            ensure(guestMove2Notify.state.board == guestMove2.state.board)
            ensure(guestMove2Notify.state.nextTurn == "X")
            ensure(guestMove2Notify.state.lastMoveActorId == options.oActorId)
            ensure(guestMove2Notify.state.lastMoveCell == 4)

            val guestSawHostWin = guestStream.waitFor<GameStateNotify>()
                .where { message -> message.payload().state.status == "Won" }
                .let { wait -> async { wait.await() } }
            val hostWin = hostStream.request(PlaceMarkReq(2)).await<PlaceMarkRes>()
            ensure(hostWin.state.board == "XXXOO....")
            ensure(hostWin.state.status == "Won")
            ensure(hostWin.state.winner == options.xActorId)

            val hostWinNotify = guestSawHostWin.await().payload()
            ensure(hostWinNotify.state.board == hostWin.state.board)
            ensure(hostWinNotify.state.status == "Won")
            ensure(hostWinNotify.state.winner == options.xActorId)
        } finally {
            hostStream.close().await()
            guestStream.close().await()
        }
    }

    private suspend fun createGame(apiUrl: String, gameName: String): CreateGameHttpRes {
        val request = HttpRequest.newBuilder(URI.create(apiUrl).resolve("/games"))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(json.writeValueAsString(CreateGameHttpReq(gameName))))
            .build()
        val response = http.sendAsync(request, HttpResponse.BodyHandlers.ofString()).await()
        ensure(response.statusCode() / 100 == 2)
        return json.readValue(response.body(), CreateGameHttpRes::class.java)
    }

    private fun playerConnector(endpoint: String): ZLinkKotlinStreamConnector =
        ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions(
                URI.create(endpoint),
                ZLinkStreamDispatchMode.AUTO,
                TicTacToeSampleDefaults.RequestTimeout,
                2,
                Duration.ofSeconds(5),
                64 * 1024,
                false,
                Duration.ofSeconds(1),
                TicTacToeSampleDefaults.RequestTimeout.plusSeconds(5),
                true,
                Duration.ofMillis(250),
                Duration.ofSeconds(5),
                2.0,
                ZLinkStreamMessagePack.codec(),
            ),
        ).kotlin()
}

private fun ensure(condition: Boolean) {
    if (!condition) {
        throw IllegalStateException("Ensure failed")
    }
}
