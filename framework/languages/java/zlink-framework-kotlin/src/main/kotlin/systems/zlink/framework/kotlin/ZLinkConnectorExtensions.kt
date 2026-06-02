package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.ZLinkStreamMessage
import systems.zlink.stream.connector.ZLinkStreamRequestCall
import systems.zlink.stream.connector.ZLinkStreamSendCall

suspend fun ZLinkStreamConnector.connect() {
    connectAsync().await()
}

suspend fun ZLinkStreamConnector.disconnect() {
    disconnectAsync().await()
}

suspend fun ZLinkStreamConnector.reconnect() {
    reconnectAsync().await()
}

suspend fun ZLinkStreamConnector.closeConnector() {
    closeAsync().await()
}

suspend fun ZLinkStreamConnector.dispatch() {
    dispatchAsync().await()
}

suspend fun ZLinkStreamSendCall.submit() {
    submitAsync().await()
}

suspend fun ZLinkStreamRequestCall.await(): ZLinkStreamEncodedPayload =
    submitAsync().await()

fun ZLinkStreamConnector.messages(
    packetName: String,
): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> = callbackFlow {
    val registration = on(packetName) { message ->
        trySend(message).getOrThrow()
        java.util.concurrent.CompletableFuture.completedFuture(null)
    }
    awaitClose {
        registration.close()
    }
}
