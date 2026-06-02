package systems.zlink.samples.kotlin.streamingclient

import java.net.URI
import java.time.Duration
import java.util.concurrent.CompletableFuture
import kotlinx.coroutines.runBlocking
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.connect
import systems.zlink.framework.kotlin.closeConnector
import systems.zlink.framework.kotlin.disconnect
import systems.zlink.framework.kotlin.dispatch
import systems.zlink.framework.kotlin.reconnect
import systems.zlink.framework.kotlin.submit
import systems.zlink.stream.connector.ZLinkStreamConnectionState
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload

fun main() = runBlocking {
    val events = mutableListOf<String>()
    val states = mutableListOf<ZLinkStreamConnectionState>()
    val connector = createConnector()
    connector.onConnectionStateChanged { state ->
        states += state
        CompletableFuture.completedFuture(null)
    }
    connector.onDisconnected {
        events += "Disconnected"
        CompletableFuture.completedFuture(null)
    }
    connector.on("GameInput") { message ->
        events += "${message.packetName()}:${message.payload().metadata()["source"]}"
        CompletableFuture.completedFuture(null)
    }
    connector.connect()
    require(connector.isConnected) { "connector did not connect" }
    require(connector.options().dispatchMode() == ZLinkStreamDispatchMode.MANUAL) {
        "connector did not keep manual dispatch option"
    }

    connector.send(payload("GameInput", "move", mapOf("source" to "send")))
        .packetName("GameInput")
        .submit()
    require(connector.pendingDispatchCount() == 1) { "manual dispatch did not queue send" }
    connector.dispatch()
    require("GameInput:send" in events) { "manual send handler not invoked" }

    val reply = connector.request(payload("GameInput", "query", mapOf("source" to "request")))
        .packetName("GameInput")
        .metadata("requestId", "r1")
        .timeout(Duration.ofSeconds(2))
        .await()
    require(reply.packetName() == "GameInput") { "request packet name mismatch" }
    connector.dispatch()
    require("GameInput:request" in events) { "manual request handler not invoked" }

    connector.closeConnector()
    require(connector.state() == ZLinkStreamConnectionState.CLOSED) { "connector did not close" }
    require(states == listOf(ZLinkStreamConnectionState.CONNECTED, ZLinkStreamConnectionState.CLOSED)) {
        "state change events mismatch"
    }
    require("Disconnected" in events) { "disconnect handler not invoked" }

    val reconnected = createConnector()
    val reconnectStates = mutableListOf<ZLinkStreamConnectionState>()
    reconnected.onConnectionStateChanged { state ->
        reconnectStates += state
        CompletableFuture.completedFuture(null)
    }
    reconnected.connect()
    reconnected.disconnect()
    reconnected.reconnect()
    require(reconnected.isConnected) { "same connector reconnect smoke failed" }
    require(
        reconnectStates == listOf(
            ZLinkStreamConnectionState.CONNECTED,
            ZLinkStreamConnectionState.DISCONNECTED,
            ZLinkStreamConnectionState.RECONNECTING,
            ZLinkStreamConnectionState.CONNECTED,
        ),
    ) { "reconnect state event mismatch" }
    reconnected.close()
    println("StreamingClient Kotlin sample self-check passed")
}

private fun createConnector(): ZLinkStreamConnector =
    ZLinkStreamConnectorFactory.create(
        ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:29200"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(3),
            2,
        ),
    )

private fun payload(
    packetName: String,
    payload: String,
    metadata: Map<String, String>,
): ZLinkStreamEncodedPayload =
    ZLinkStreamEncodedPayload(packetName, Message.from(payload), metadata)
