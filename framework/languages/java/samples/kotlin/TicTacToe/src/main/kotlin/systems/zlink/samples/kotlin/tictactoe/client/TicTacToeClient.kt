package systems.zlink.samples.kotlin.tictactoe.client

import java.net.URI
import java.time.Duration
import kotlinx.coroutines.future.await
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.json.ZLinkStreamJson

class TicTacToeClient(
    private val frameworkClient: ZLinkClient,
) {
    suspend fun run(options: TicTacToeClientOptions): TicTacToeClientResult {
        val host = authenticate(options.hostAccessToken)
        val guest = authenticate(options.guestAccessToken)
        val game = createGame(options.gameName)
        val hostStream = playerConnector(options.playEndpoint, host.actorId)
        val guestStream = playerConnector(options.playEndpoint, guest.actorId)
        try {
            hostStream.connectAsync().await()
            guestStream.connectAsync().await()

            request(hostStream, AuthenticateReq(host.actorId), AuthenticateRes::class.java)
            request(guestStream, AuthenticateReq(guest.actorId), AuthenticateRes::class.java)
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

    private suspend fun authenticate(accessToken: String): AuthenticateRes =
        AuthenticateRes(
            frameworkClient
                .requestToChannel("tictactoe-api", AuthenticatePlayerReq(accessToken))
                .packetName("AuthenticatePlayer")
                .awaitReply<AuthenticatePlayerRes>()
                .actorId,
        )

    private suspend fun createGame(gameName: String): CreateGameRes {
        return frameworkClient
            .requestToChannel("tictactoe-api", CreateGameReq(gameName))
            .packetName("CreateGame")
            .awaitReply<CreateGameRes>()
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
