package systems.zlink.framework.kotlin

import java.net.URI
import java.time.Duration
import kotlin.coroutines.Continuation
import kotlin.coroutines.EmptyCoroutineContext
import kotlin.coroutines.startCoroutine
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import systems.zlink.contracts.messaging.Message
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload

final class KotlinConnectorWrapperTest {
    @Test
    fun suspendWrapperPreservesConnectorSemantics() {
        runSuspend {
            ZLinkStreamConnectorFactory.create(options()).use { connector ->
                val handled = mutableListOf<String>()
                connector.on("Echo") { message ->
                    handled.add(message.payload().payload().toUtf8String())
                    java.util.concurrent.CompletableFuture.completedFuture(null)
                }

                connector.connect()
                assertTrue(connector.isConnected)

                connector.send(payload("Echo", "send")).submit()
                connector.dispatchAsync().toCompletableFuture().join()
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
    }

    private fun runSuspend(block: suspend () -> Unit) {
        var failure: Throwable? = null
        block.startCoroutine(object : Continuation<Unit> {
            override val context = EmptyCoroutineContext

            override fun resumeWith(result: Result<Unit>) {
                failure = result.exceptionOrNull()
            }
        })
        failure?.let { throw it }
    }

    private fun options() =
        ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:7200"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(1),
            1,
        )

    private fun payload(packetName: String, body: String) =
        ZLinkStreamEncodedPayload(packetName, Message.from(body), mapOf())
}
