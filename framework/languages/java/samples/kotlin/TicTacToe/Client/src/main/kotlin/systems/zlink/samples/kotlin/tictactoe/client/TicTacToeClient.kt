package systems.zlink.samples.kotlin.tictactoe.client

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration
import kotlinx.coroutines.future.await
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.json.ZLinkStreamJson

class TicTacToeClient {
    private val http = HttpClient.newHttpClient()
    private val json = ObjectMapper().registerKotlinModule()

    suspend fun run(options: TicTacToeClientOptions): TicTacToeClientResult {
        val game = createGame(options.apiUrl, options.gameName)
        val hostStream = playerConnector(game.playEndpoint, options.xActorId)
        val guestStream = playerConnector(game.playEndpoint, options.oActorId)
        try {
            hostStream.connectAsync().await()
            guestStream.connectAsync().await()

            request(hostStream, AuthenticateReq(options.xActorId), AuthenticateRes::class.java)
            request(guestStream, AuthenticateReq(options.oActorId), AuthenticateRes::class.java)
            request(hostStream, JoinGameReq(game.gameId), JoinGameRes::class.java)
            request(guestStream, JoinGameReq(game.gameId), JoinGameRes::class.java)
            request(hostStream, PlaceMarkReq(0), PlaceMarkRes::class.java)
            request(guestStream, PlaceMarkReq(4), PlaceMarkRes::class.java)
            request(hostStream, PlaceMarkReq(1), PlaceMarkRes::class.java)
            request(guestStream, PlaceMarkReq(8), PlaceMarkRes::class.java)
            val finalReply = request(hostStream, PlaceMarkReq(2), PlaceMarkRes::class.java)

            return TicTacToeClientResult(
                winner = finalReply.state.winner,
                pushes = finalReply.state.winner?.let { listOf("GameWon:$it") }.orEmpty(),
            )
        } finally {
            hostStream.close()
            guestStream.close()
        }
    }

    private suspend fun createGame(apiUrl: String, gameName: String): CreateGameHttpRes {
        val request = HttpRequest.newBuilder(URI.create(apiUrl).resolve("/games"))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(json.writeValueAsString(CreateGameHttpReq(gameName))))
            .build()
        val response = http.sendAsync(request, HttpResponse.BodyHandlers.ofString()).await()
        check(response.statusCode() / 100 == 2) {
            "API returned HTTP ${response.statusCode()}: ${response.body()}"
        }
        return json.readValue(response.body(), CreateGameHttpRes::class.java)
    }

    private fun playerConnector(endpoint: String, actorId: String): ZLinkStreamConnector =
        ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions(
                URI.create("$endpoint/$actorId"),
                ZLinkStreamDispatchMode.MANUAL,
                Duration.ofSeconds(3),
                2,
            ),
        )

    private suspend fun <TReply> request(
        connector: ZLinkStreamConnector,
        request: Any,
        replyType: Class<TReply>,
    ): TReply =
        ZLinkStreamJson.request(connector, request)
            .submitAsync()
            .await()
            .let { reply -> ZLinkStreamJson.decode(reply, replyType) }
}
