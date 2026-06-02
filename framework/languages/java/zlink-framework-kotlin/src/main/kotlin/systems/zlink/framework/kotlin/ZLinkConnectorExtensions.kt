package systems.zlink.framework.kotlin

import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.ZLinkStreamRequestCall
import systems.zlink.stream.connector.ZLinkStreamSendCall

suspend fun ZLinkStreamConnector.connect() {
    connectAsync().toCompletableFuture().join()
}

suspend fun ZLinkStreamConnector.disconnect() {
    disconnectAsync().toCompletableFuture().join()
}

suspend fun ZLinkStreamConnector.reconnect() {
    reconnectAsync().toCompletableFuture().join()
}

suspend fun ZLinkStreamConnector.closeConnector() {
    closeAsync().toCompletableFuture().join()
}

suspend fun ZLinkStreamSendCall.submit() {
    submitAsync().toCompletableFuture().join()
}

suspend fun ZLinkStreamRequestCall.await(): ZLinkStreamEncodedPayload =
    submitAsync().toCompletableFuture().join()
