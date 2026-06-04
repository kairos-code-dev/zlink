package systems.zlink.samples.kotlin.tictactoe.client

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.future.await
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
        val hostStream = playerConnector(game.playEndpoint)
        val guestStream = playerConnector(game.playEndpoint)
        val stateNotifications = ConcurrentLinkedQueue<GameStateNotify>()
        val playerJoinedNotifications = ConcurrentLinkedQueue<PlayerJoinedNotify>()
        val handlers = listOf(
            ZLinkStreamJson.on(hostStream, GameStateNotify::class.java) { message ->
                stateNotifications += message.payload()
                CompletableFuture.completedFuture(null)
            },
            ZLinkStreamJson.on(guestStream, GameStateNotify::class.java) { message ->
                stateNotifications += message.payload()
                CompletableFuture.completedFuture(null)
            },
            ZLinkStreamJson.on(hostStream, PlayerJoinedNotify::class.java) { message ->
                playerJoinedNotifications += message.payload()
                CompletableFuture.completedFuture(null)
            },
            ZLinkStreamJson.on(guestStream, PlayerJoinedNotify::class.java) { message ->
                playerJoinedNotifications += message.payload()
                CompletableFuture.completedFuture(null)
            },
        )
        try {
            hostStream.connectAsync().await()
            guestStream.connectAsync().await()

            val xAuthentication = requestStep("host AuthenticateReq") {
                request(hostStream, AuthenticateReq(options.xActorId), AuthenticateRes::class.java)
            }
            val oAuthentication = requestStep("guest AuthenticateReq") {
                request(guestStream, AuthenticateReq(options.oActorId), AuthenticateRes::class.java)
            }
            val xJoin = requestStep("host JoinGameReq") {
                request(hostStream, JoinGameReq(game.gameId), JoinGameRes::class.java)
            }
            val oJoin = requestStep("guest JoinGameReq") {
                request(guestStream, JoinGameReq(game.gameId), JoinGameRes::class.java)
            }
            val moves = listOf(
                requestStep("host PlaceMarkReq(0)") {
                    request(hostStream, PlaceMarkReq(0), PlaceMarkRes::class.java)
                },
                requestStep("guest PlaceMarkReq(3)") {
                    request(guestStream, PlaceMarkReq(3), PlaceMarkRes::class.java)
                },
                requestStep("host PlaceMarkReq(1)") {
                    request(hostStream, PlaceMarkReq(1), PlaceMarkRes::class.java)
                },
                requestStep("guest PlaceMarkReq(4)") {
                    request(guestStream, PlaceMarkReq(4), PlaceMarkRes::class.java)
                },
                requestStep("host PlaceMarkReq(2)") {
                    request(hostStream, PlaceMarkReq(2), PlaceMarkRes::class.java)
                },
            )

            validateFinalState(options, moves)

            CompletableFuture.runAsync(
                {},
                CompletableFuture.delayedExecutor(250, TimeUnit.MILLISECONDS),
            ).await()

            return TicTacToeClientResult(
                game = game,
                xAuthentication = xAuthentication,
                oAuthentication = oAuthentication,
                xJoin = xJoin,
                oJoin = oJoin,
                moves = moves,
                stateNotifications = stateNotifications.toList(),
                playerJoinedNotifications = playerJoinedNotifications.toList(),
            )
        } finally {
            handlers.forEach { handler -> handler.close() }
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

    private fun playerConnector(endpoint: String): ZLinkStreamConnector =
        ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions(
                URI.create(endpoint),
                ZLinkStreamDispatchMode.AUTO,
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

    private suspend fun <TReply> requestStep(
        step: String,
        request: suspend () -> TReply,
    ): TReply =
        try {
            request()
        } catch (error: Throwable) {
            throw IllegalStateException("TicTacToe sample step failed: $step", error)
        }

    private fun validateFinalState(options: TicTacToeClientOptions, moves: List<PlaceMarkRes>) {
        val finalState = moves.lastOrNull()?.state
            ?: throw IllegalStateException("TicTacToe sample did not complete any moves.")
        check(finalState.status == "Won" && finalState.winner == options.xActorId) {
            "Unexpected final game state: board=${finalState.board}, status=${finalState.status}, winner=${finalState.winner ?: "-"}"
        }
    }
}
