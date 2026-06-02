package systems.zlink.samples.kotlin.bingo.client

import java.net.URI
import java.time.Duration
import java.util.concurrent.CompletableFuture
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.kotlin.connect
import systems.zlink.framework.kotlin.dispatch
import systems.zlink.framework.kotlin.submit
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload

class BingoPlayerClient(
    val playerId: String,
) : AutoCloseable {
    private val connector: ZLinkStreamConnector =
        ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions(
                URI.create("tcp://127.0.0.1:29100/$playerId"),
                ZLinkStreamDispatchMode.MANUAL,
                Duration.ofSeconds(3),
                2,
            ),
        )
    val inbox = BingoNotificationInbox()

    suspend fun connect() {
        connector.on("BingoWinner") { message ->
            inbox.add(message.payload().payload().toUtf8String())
            CompletableFuture.completedFuture(null)
        }
        connector.connect()
    }

    suspend fun push(packetName: String, payload: String, roomId: String) {
        connector.send(
            ZLinkStreamEncodedPayload(
                packetName,
                Message.from(payload),
                mapOf("roomId" to roomId),
            ),
        )
            .packetName(packetName)
            .metadata("playerId", playerId)
            .submit()
    }

    suspend fun dispatch() {
        connector.dispatch()
    }

    override fun close() {
        connector.close()
    }
}
