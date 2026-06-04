package systems.zlink.samples.kotlin.bingo.client

import java.net.URI
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import java.util.concurrent.TimeoutException
import systems.zlink.framework.kotlin.connect
import systems.zlink.framework.kotlin.dispatch
import kotlinx.coroutines.future.await
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameEndedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameStartedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoNumberDrawnNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinner
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.PlayerJoinedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameRes
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.json.ZLinkStreamJson

class BingoPlayerClient(
    val playerId: String,
) : AutoCloseable {
    private val connector: ZLinkStreamConnector =
        ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions(
                URI.create(SampleTopology.StreamEndpoint),
                ZLinkStreamDispatchMode.MANUAL,
                SampleTimings.RequestTimeout,
                2,
                SampleTimings.ConnectTimeout,
                64 * 1024,
                false,
                java.time.Duration.ofSeconds(1),
                SampleTimings.RequestTimeout.plusSeconds(5),
                true,
                java.time.Duration.ofMillis(250),
                java.time.Duration.ofSeconds(5),
                2.0,
            ),
        )
    val inbox = BingoNotificationInbox()

    suspend fun connect() {
        connector.on(SampleNames.PlayerJoinedPacket) { message ->
            inbox.playerJoined(ZLinkStreamJson.decode(message.payload(), PlayerJoinedNotify::class.java))
            CompletableFuture.completedFuture(null)
        }
        connector.on(SampleNames.GameStartedPacket) { message ->
            inbox.started(ZLinkStreamJson.decode(message.payload(), BingoGameStartedNotify::class.java))
            CompletableFuture.completedFuture(null)
        }
        connector.on(SampleNames.NumberDrawnPacket) { message ->
            inbox.drawn(ZLinkStreamJson.decode(message.payload(), BingoNumberDrawnNotify::class.java))
            CompletableFuture.completedFuture(null)
        }
        connector.on(SampleNames.GameEndedPacket) { message ->
            inbox.ended(ZLinkStreamJson.decode(message.payload(), BingoGameEndedNotify::class.java))
            CompletableFuture.completedFuture(null)
        }
        connector.connect()
    }

    suspend fun authenticate(): AuthenticateRes =
        awaitWithDispatch(
            ZLinkStreamJson.request(connector, AuthenticateReq(playerId))
            .packetName("AuthenticateReq")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(),
        )
            .let { ZLinkStreamJson.decode(it, AuthenticateRes::class.java) }

    suspend fun match(): MatchBingoRes {
        var last: TimeoutException? = null
        repeat(3) {
            try {
                return awaitWithDispatch(
                    ZLinkStreamJson.request(connector, MatchBingoReq("four-player"))
                    .packetName("MatchBingoReq")
                    .timeout(SampleTimings.RequestTimeout)
                    .submitAsync(),
                )
                    .let { ZLinkStreamJson.decode(it, MatchBingoRes::class.java) }
            } catch (ex: TimeoutException) {
                last = ex
            }
        }
        throw last ?: TimeoutException("MatchBingoReq timed out")
    }

    suspend fun start(roomId: String): StartBingoGameRes =
        awaitWithDispatch(
            ZLinkStreamJson.request(connector, StartBingoGameReq(roomId))
            .packetName("StartBingoGameReq")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(),
        )
            .let { ZLinkStreamJson.decode(it, StartBingoGameRes::class.java) }

    suspend fun dispatch() {
        connector.dispatch()
    }

    fun winners(): List<BingoWinner> = inbox.winners()

    private suspend fun <T> awaitWithDispatch(stage: CompletionStage<T>): T {
        val result = CompletableFuture<T>()
        stage.whenComplete { value, error ->
            if (error != null) {
                result.completeExceptionally(error)
            } else {
                result.complete(value)
            }
        }
        while (!result.isDone) {
            connector.dispatch()
            Thread.onSpinWait()
        }
        return result.await()
    }

    override fun close() {
        connector.close()
    }
}
