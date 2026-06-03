package systems.zlink.samples.kotlin.tictactoe.client

import java.net.URI
import java.time.Duration
import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload

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

            request(hostStream, "AuthenticateReq", host.actorId)
            request(guestStream, "AuthenticateReq", guest.actorId)
            request(hostStream, "JoinGameReq", "${game.gameId}|${host.actorId}")
            request(guestStream, "JoinGameReq", "${game.gameId}|${guest.actorId}")
            request(hostStream, "PlaceMarkReq", "${game.gameId}|${host.actorId}|0")
            request(guestStream, "PlaceMarkReq", "${game.gameId}|${guest.actorId}|4")
            request(hostStream, "PlaceMarkReq", "${game.gameId}|${host.actorId}|1")
            request(guestStream, "PlaceMarkReq", "${game.gameId}|${guest.actorId}|8")
            val finalReply = request(hostStream, "PlaceMarkReq", "${game.gameId}|${host.actorId}|2")
            val parts = finalReply.split("|", limit = 2)

            return TicTacToeClientResult(
                winner = parts[0].ifBlank { null },
                pushes = parts.getOrElse(1) { "" }.split(",").filter { it.isNotBlank() },
            )
        } finally {
            hostStream.close()
            guestStream.close()
        }
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
        return CreateGameRes(gameId, "tcp://127.0.0.1:47302", gameName)
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

    private suspend fun request(
        connector: ZLinkStreamConnector,
        packetName: String,
        body: String,
    ): String =
        connector.request(
            ZLinkStreamEncodedPayload(packetName, Message.from(body), emptyMap()),
        )
            .packetName(packetName)
            .submitAsync()
            .await()
            .payload()
            .toUtf8String()
}
