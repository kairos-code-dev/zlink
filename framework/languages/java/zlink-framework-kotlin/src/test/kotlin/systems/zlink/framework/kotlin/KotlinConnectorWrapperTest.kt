package systems.zlink.framework.kotlin

import java.net.URI
import java.time.Duration
import java.time.Duration.ofSeconds
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionException
import java.util.concurrent.CompletionStage
import java.util.concurrent.TimeUnit
import java.util.Optional
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.assertThrows
import org.junit.jupiter.api.Test
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkSendCall
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload

final class KotlinConnectorWrapperTest {
    @Test
    fun suspendWrapperPreservesConnectorSemantics() = runBlocking {
            ZLinkStreamConnectorFactory.create(options()).use { connector ->
                val handled = mutableListOf<String>()
                connector.on("Echo") { message ->
                    handled.add(message.payload().payload().toUtf8String())
                    CompletableFuture.completedFuture(null)
                }

                connector.connect()
                assertTrue(connector.isConnected)

                connector.send(payload("Echo", "send")).submit()
                connector.dispatch()
                assertEquals(listOf("send"), handled)

                val reply = connector.request(payload("Echo", "request")).await()
                try {
                    assertEquals("request", reply.payload().toUtf8String())
                } finally {
                    reply.payload().close()
                }

                connector.disconnect()
                connector.reconnect()
                assertTrue(connector.isConnected)
            }
    }

    @Test
    fun frameworkSubmitAndRequestWrappersAwaitCompletionStage() = runBlocking {
        var submitted = false

        object : ZLinkSendCall {
            override fun packetName(packetName: String): ZLinkSendCall = this
            override fun metadata(key: String, value: String): ZLinkSendCall = this
            override fun submitAsync(): CompletionStage<Void> {
                submitted = true
                return CompletableFuture.completedFuture(null)
            }
        }.submit()

        val reply = object : ZLinkRequestCall {
            override fun packetName(packetName: String): ZLinkRequestCall = this
            override fun metadata(key: String, value: String): ZLinkRequestCall = this
            override fun timeout(timeout: Duration): ZLinkRequestCall = this
            override fun <TReply> submitAsync(replyType: Class<TReply>): CompletionStage<TReply> =
                CompletableFuture.completedFuture(replyType.cast("reply"))
        }.awaitReply<String>()

        assertTrue(submitted)
        assertEquals("reply", reply)
    }

    @Test
    fun coroutineRuntimeMapsSuspendHandlerToCompletionStage() {
        ZLinkCoroutineRuntime().use { runtime ->
            val handler = runtime.requestHandler<String, String> { request, _ -> "$request/reply" }

            assertEquals(
                "request/reply",
                handler.handleAsync("request", requestContext()).toCompletableFuture().get(1, TimeUnit.SECONDS),
            )
        }
    }

    @Test
    fun coroutineRuntimeCompletesStageExceptionallyWhenHandlerThrows() {
        ZLinkCoroutineRuntime().use { runtime ->
            val handler = runtime.requestHandler<String, String> { _, _ ->
                throw IllegalStateException("boom")
            }

            val failure = assertThrows<CompletionException> {
                handler.handleAsync("request", requestContext()).toCompletableFuture().join()
            }

            assertTrue(failure.cause is IllegalStateException)
        }
    }

    @Test
    fun closingCoroutineRuntimeCancelsInFlightHandler() {
        val runtime = ZLinkCoroutineRuntime()
        val handler = runtime.requestHandler<String, String> { _, _ ->
            awaitCancellation()
        }
        val future = handler.handleAsync("request", requestContext()).toCompletableFuture()

        runtime.close()

        assertThrows<Exception> {
            future.get(1, TimeUnit.SECONDS)
        }
        assertTrue(future.isCancelled || future.isCompletedExceptionally)
    }

    private fun options() =
        ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:7200"),
            ZLinkStreamDispatchMode.MANUAL,
            ofSeconds(1),
            1,
        )

    private fun payload(packetName: String, body: String) =
        ZLinkStreamEncodedPayload(packetName, Message.from(body), mapOf())

    private fun requestContext() =
        object : ZLinkRequestContext {
            override fun channelName(): Optional<String> = Optional.of("test")
            override fun packetName(): Optional<String> = Optional.of("request")
            override fun contentType(): Optional<String> = Optional.empty()
            override fun cancellationToken(): CancellationToken =
                CancellationToken { false }
        }
}
