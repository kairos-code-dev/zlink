package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import java.time.Duration
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.ZLinkStreamError
import systems.zlink.stream.connector.ZLinkStreamLifecycleCall
import systems.zlink.stream.connector.ZLinkStreamMessage
import systems.zlink.stream.connector.ZLinkStreamRequestCall
import systems.zlink.stream.connector.ZLinkStreamSendCall
import systems.zlink.stream.connector.ZLinkStreamTypedRequestCall
import systems.zlink.stream.connector.ZLinkStreamWaitCall

fun ZLinkStreamConnector.kotlin(): ZLinkKotlinStreamConnector =
    ZLinkKotlinStreamConnector(this)

class ZLinkKotlinStreamConnector(
    @PublishedApi
    internal val inner: ZLinkStreamConnector,
) {
    val isConnected: Boolean
        get() = inner.isConnected

    fun receivedCount(name: String): Int =
        inner.receivedCount(name)

    fun connect(): ZLinkKotlinLifecycleCall =
        ZLinkKotlinLifecycleCall(inner.connect())

    fun disconnect(): ZLinkKotlinLifecycleCall =
        ZLinkKotlinLifecycleCall(inner.disconnect())

    fun reconnect(): ZLinkKotlinLifecycleCall =
        ZLinkKotlinLifecycleCall(inner.reconnect())

    fun close(): ZLinkKotlinLifecycleCall =
        ZLinkKotlinLifecycleCall(inner.close())

    fun dispatch(): ZLinkKotlinLifecycleCall =
        ZLinkKotlinLifecycleCall(inner.dispatch())

    fun send(payload: ZLinkStreamEncodedPayload): ZLinkKotlinSendCall =
        ZLinkKotlinSendCall(inner.send(payload))

    fun send(payload: Any): ZLinkKotlinSendCall =
        ZLinkKotlinSendCall(inner.send(payload))

    fun request(payload: ZLinkStreamEncodedPayload): ZLinkStreamRequestCall =
        inner.request(payload)

    fun request(payload: Any): ZLinkStreamTypedRequestCall =
        inner.request(payload)

    inline fun <reified TPayload> waitFor(): ZLinkStreamTypedWaitCall<TPayload> =
        ZLinkStreamTypedWaitCall(inner.waitFor(TPayload::class.java), TPayload::class.java)

    inline fun <reified TPayload> waitFor(name: String): ZLinkStreamTypedWaitCall<TPayload> =
        ZLinkStreamTypedWaitCall(inner.waitFor(name), TPayload::class.java)

    fun messages(packetName: String): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> =
        inner.messages(packetName)

    fun errors(): Flow<ZLinkStreamError> =
        inner.errors()
}

class ZLinkKotlinLifecycleCall(
    private val inner: ZLinkStreamLifecycleCall,
) {
    suspend fun await() {
        inner.submit().await()
    }
}

class ZLinkKotlinSendCall(
    private val inner: ZLinkStreamSendCall,
) {
    suspend fun await() {
        inner.submit().await()
    }
}

suspend fun ZLinkStreamRequestCall.await(): ZLinkStreamEncodedPayload =
    submit().await()

suspend inline fun <reified TReply> ZLinkStreamTypedRequestCall.await(): TReply =
    submit(TReply::class.java).await()

suspend inline fun <reified TPayload> ZLinkStreamWaitCall.await(): ZLinkStreamMessage<TPayload> =
    submit(TPayload::class.java).await()

inline fun <reified TPayload> ZLinkStreamConnector.waitFor(): ZLinkStreamTypedWaitCall<TPayload> =
    ZLinkStreamTypedWaitCall(waitFor(TPayload::class.java), TPayload::class.java)

class ZLinkStreamTypedWaitCall<TPayload>(
    private val inner: ZLinkStreamWaitCall,
    private val payloadType: Class<TPayload>,
) {
    fun timeout(timeout: Duration): ZLinkStreamTypedWaitCall<TPayload> =
        ZLinkStreamTypedWaitCall(inner.timeout(timeout), payloadType)

    fun where(
        predicate: (ZLinkStreamMessage<TPayload>) -> Boolean,
    ): ZLinkStreamTypedWaitCall<TPayload> =
        ZLinkStreamTypedWaitCall(inner.where(payloadType, predicate), payloadType)

    suspend fun await(): ZLinkStreamMessage<TPayload> =
        inner.submit(payloadType).await()
}

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

fun ZLinkStreamConnector.errors(): Flow<ZLinkStreamError> = callbackFlow {
    val registration = onErrorReceived { error ->
        trySend(error).getOrThrow()
        java.util.concurrent.CompletableFuture.completedFuture(null)
    }
    awaitClose {
        registration.close()
    }
}
