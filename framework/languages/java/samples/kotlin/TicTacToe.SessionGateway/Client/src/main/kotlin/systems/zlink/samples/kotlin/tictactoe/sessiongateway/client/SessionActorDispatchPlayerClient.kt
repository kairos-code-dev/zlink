package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

import java.net.URI
import java.time.Duration
import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload

class SessionActorDispatchPlayerClient private constructor(
    private val actorId: String,
    private val notifications: SessionActorNotificationInbox,
    private val connector: ZLinkStreamConnector,
) : AutoCloseable {
    companion object {
        suspend fun connect(
            actorId: String,
            streamEndpoint: String,
        ): SessionActorDispatchPlayerClient {
            val connector = ZLinkStreamConnectorFactory.create(
                ZLinkStreamConnectorOptions(
                    URI.create("$streamEndpoint/$actorId"),
                    ZLinkStreamDispatchMode.MANUAL,
                    Duration.ofSeconds(10),
                    2,
                ),
            )
            connector.connectAsync().await()
            val inbox = SessionActorNotificationInbox()
            inbox.register(connector)
            return SessionActorDispatchPlayerClient(actorId, inbox, connector)
        }
    }

    suspend fun authenticate(): AuthenticateRes = AuthenticateRes(request("AuthenticateReq", actorId))

    suspend fun createMatch(): CreateMatchRes {
        val parts = request("CreateMatchReq", "").split("|", limit = 2)
        return CreateMatchRes(parts[0], parts[1])
    }

    suspend fun join(matchId: String): JoinMatchRes {
        val parts = request("JoinMatchReq", matchId).split("|", limit = 4)
        return JoinMatchRes(parts[0], parts[1], parts[2], TicTacToeState.decode(parts[3]))
    }

    suspend fun placeMark(matchId: String, cell: Int): PlaceMarkRes =
        PlaceMarkRes(TicTacToeState.decode(request("PlaceMarkReq", "$matchId|$cell")))

    suspend fun dispatch() {
        connector.dispatchAsync().await()
    }

    fun notificationCount(): Int =
        turnChangedNotifications().size + opponentJoinedNotifications().size + gameEndedNotifications().size

    fun turnChangedNotifications(): List<String> = notifications.turnChanged()
    fun opponentJoinedNotifications(): List<String> = notifications.opponentJoined()
    fun gameEndedNotifications(): List<String> = notifications.gameEnded()

    override fun close() {
        connector.close()
    }

    private suspend fun request(packetName: String, body: String): String =
        connector.request(ZLinkStreamEncodedPayload(packetName, Message.from(body), emptyMap()))
            .packetName(packetName)
            .submitAsync()
            .await()
            .payload()
            .toUtf8String()
}

data class AuthenticateRes(val actorId: String)
data class CreateMatchRes(val matchId: String, val ownerActorId: String)
data class JoinMatchRes(
    val matchId: String,
    val actorId: String,
    val mark: String,
    val state: TicTacToeState,
)
data class PlaceMarkRes(val state: TicTacToeState)
data class TicTacToeState(
    val matchId: String,
    val board: String,
    val status: String,
    val turnActorId: String?,
    val winnerActorId: String?,
    val draw: Boolean,
    val xActorId: String?,
    val oActorId: String?,
    val lastMoveActorId: String?,
    val lastMoveCell: Int?,
) {
        companion object {
        fun decode(value: String): TicTacToeState {
            val parts = value.split(",", limit = 10)
            return TicTacToeState(
                matchId = parts[0],
                board = parts[1],
                status = parts[2],
                turnActorId = parts[3].ifEmpty { null },
                winnerActorId = parts[4].ifEmpty { null },
                draw = parts[5].toBoolean(),
                xActorId = parts[6].ifEmpty { null },
                oActorId = parts[7].ifEmpty { null },
                lastMoveActorId = parts[8].ifEmpty { null },
                lastMoveCell = parts[9].ifEmpty { null }?.toInt(),
            )
        }
    }
}
