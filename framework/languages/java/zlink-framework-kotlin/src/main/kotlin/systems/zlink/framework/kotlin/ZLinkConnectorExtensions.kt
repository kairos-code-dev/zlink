package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
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
