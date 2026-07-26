package systems.zlink.framework.kotlin

import java.time.Duration
import java.util.concurrent.CompletionStage
import kotlin.reflect.KClass
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorClient
import systems.zlink.framework.actors.ZLinkBoundSession
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkSendCall
import systems.zlink.framework.spots.ZLinkWorkerCall
import systems.zlink.framework.spots.ZLinkSpotRequestCall
import systems.zlink.framework.spots.ZLinkSpotSendCall
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionClient

/**
 * Kotlin one-way terminal. Successful completion means local queue admission;
 * delivery and handler completion are outside this result boundary.
 */
interface ZLinkKotlinMessageSendCall {
    fun metadata(key: String, value: String): ZLinkKotlinMessageSendCall
    suspend fun await()
}

interface ZLinkKotlinSubmissionCall {
    suspend fun await()
}

interface ZLinkKotlinRequestCall<TReply : Any> {
    fun metadata(key: String, value: String): ZLinkKotlinRequestCall<TReply>
    fun timeout(timeout: Duration): ZLinkKotlinRequestCall<TReply>
    suspend fun await(): TReply
    suspend fun yield(): TReply
}

interface ZLinkKotlinClient {
    fun sendToChannel(channelName: String, message: Any): ZLinkKotlinMessageSendCall

    fun <TReply : Any> requestToChannel(
        channelName: String,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply>
}

interface ZLinkKotlinFanoutClient {
    fun publish(
        channelName: String,
        topic: String,
        event: Any,
    ): ZLinkKotlinSubmissionCall

    fun publish(channelName: String, event: Any): ZLinkKotlinSubmissionCall
}

interface ZLinkKotlinRouteClient {
    fun sendToNode(
        meshName: String,
        target: RoutingId,
        message: Any,
    ): ZLinkKotlinMessageSendCall

    fun <TReply : Any> requestToNode(
        meshName: String,
        target: RoutingId,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply>

    fun sendToChannel(channelName: String, message: Any): ZLinkKotlinMessageSendCall

    fun <TReply : Any> requestToChannel(
        channelName: String,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply>
}

interface ZLinkKotlinActorClient {
    fun sendToActor(actorId: String, message: Any): ZLinkKotlinMessageSendCall

    fun <TReply : Any> requestToActor(
        actorId: String,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply>
}

interface ZLinkKotlinSpotSendCall {
    fun metadata(key: String, value: String): ZLinkKotlinSpotSendCall
    fun instanceSpot(): ZLinkKotlinSpotSendCall
    fun instanceSpot(stableType: String): ZLinkKotlinSpotSendCall
    fun inMesh(meshName: String): ZLinkKotlinSpotSendCall
    suspend fun await()
}

interface ZLinkKotlinSpotRequestCall<TReply : Any> {
    fun metadata(key: String, value: String): ZLinkKotlinSpotRequestCall<TReply>
    fun instanceSpot(): ZLinkKotlinSpotRequestCall<TReply>
    fun instanceSpot(stableType: String): ZLinkKotlinSpotRequestCall<TReply>
    fun inMesh(meshName: String): ZLinkKotlinSpotRequestCall<TReply>
    fun timeout(timeout: Duration): ZLinkKotlinSpotRequestCall<TReply>
    suspend fun await(): TReply
    suspend fun yield(): TReply
}

interface ZLinkKotlinSessionSendCall {
    fun metadata(key: String, value: String): ZLinkKotlinSessionSendCall
    fun compress(): ZLinkKotlinSessionSendCall
    suspend fun await()
}

interface ZLinkKotlinSessionReplyCall {
    fun compress(): ZLinkKotlinSessionReplyCall
    suspend fun await()
}

interface ZLinkKotlinSessionClient {
    fun send(message: Any): ZLinkKotlinSessionSendCall
    fun reply(message: Any): ZLinkKotlinSessionReplyCall
}

interface ZLinkKotlinSessionActor {
    fun relay(message: systems.zlink.framework.messaging.ZLinkMessage):
        ZLinkKotlinSubmissionCall
    fun relay(
        dispatch: systems.zlink.framework.streams.ZLinkSessionMessageContext,
        message: systems.zlink.framework.messaging.ZLinkMessage,
    ): ZLinkKotlinSubmissionCall
}

interface ZLinkKotlinBoundSession {
    fun send(message: Any): ZLinkKotlinMessageSendCall
}

interface ZLinkKotlinWorkerCall<T> {
    suspend fun await(): T
    suspend fun yield(): T
}

private class JavaMessageSendCall(
    private val call: ZLinkSendCall,
) : ZLinkKotlinMessageSendCall {
    override fun metadata(key: String, value: String): ZLinkKotlinMessageSendCall =
        JavaMessageSendCall(call.metadata(key, value))

    override suspend fun await() {
        awaitFrameworkStage(call.submit())
    }
}

private class DeferredMessageSendCall(
    private val submit: (Map<String, String>) -> CompletionStage<Void>,
    private val metadata: Map<String, String> = emptyMap(),
) : ZLinkKotlinMessageSendCall {
    override fun metadata(key: String, value: String): ZLinkKotlinMessageSendCall =
        DeferredMessageSendCall(submit, metadata + (key to value))

    override suspend fun await() {
        awaitFrameworkStage(submit(metadata))
    }
}

private class DeferredSubmissionCall(
    private val submit: () -> CompletionStage<Void>,
) : ZLinkKotlinSubmissionCall {
    override suspend fun await() {
        awaitFrameworkStage(submit())
    }
}

private class JavaRequestCall<TReply : Any>(
    private val call: ZLinkRequestCall,
    private val replyType: Class<TReply>,
) : ZLinkKotlinRequestCall<TReply> {
    override fun metadata(key: String, value: String): ZLinkKotlinRequestCall<TReply> =
        JavaRequestCall(call.metadata(key, value), replyType)

    override fun timeout(timeout: Duration): ZLinkKotlinRequestCall<TReply> =
        JavaRequestCall(call.timeout(timeout), replyType)

    override suspend fun await(): TReply =
        awaitFrameworkStage(call.submit(replyType))

    override suspend fun yield(): TReply =
        awaitFrameworkStage(call.yield(replyType))
}

private class JavaSpotSendCall(
    private val call: ZLinkSpotSendCall,
) : ZLinkKotlinSpotSendCall {
    override fun metadata(key: String, value: String): ZLinkKotlinSpotSendCall =
        JavaSpotSendCall(call.metadata(key, value))

    override fun instanceSpot(): ZLinkKotlinSpotSendCall =
        JavaSpotSendCall(call.instanceSpot())

    override fun instanceSpot(stableType: String): ZLinkKotlinSpotSendCall =
        JavaSpotSendCall(call.instanceSpot(stableType))

    override fun inMesh(meshName: String): ZLinkKotlinSpotSendCall =
        JavaSpotSendCall(call.inMesh(meshName))

    override suspend fun await() {
        awaitFrameworkStage(call.submit())
    }
}

private class JavaSpotRequestCall<TReply : Any>(
    private val call: ZLinkSpotRequestCall,
    private val replyType: Class<TReply>,
) : ZLinkKotlinSpotRequestCall<TReply> {
    override fun metadata(key: String, value: String): ZLinkKotlinSpotRequestCall<TReply> =
        JavaSpotRequestCall(call.metadata(key, value), replyType)

    override fun instanceSpot(): ZLinkKotlinSpotRequestCall<TReply> =
        JavaSpotRequestCall(call.instanceSpot(), replyType)

    override fun instanceSpot(stableType: String): ZLinkKotlinSpotRequestCall<TReply> =
        JavaSpotRequestCall(call.instanceSpot(stableType), replyType)

    override fun inMesh(meshName: String): ZLinkKotlinSpotRequestCall<TReply> =
        JavaSpotRequestCall(call.inMesh(meshName), replyType)

    override fun timeout(timeout: Duration): ZLinkKotlinSpotRequestCall<TReply> =
        JavaSpotRequestCall(call.timeout(timeout), replyType)

    override suspend fun await(): TReply =
        awaitFrameworkStage(call.submit(replyType))

    override suspend fun yield(): TReply =
        awaitFrameworkStage(call.yield(replyType))
}

private class JavaClient(
    private val client: ZLinkClient,
) : ZLinkKotlinClient {
    override fun sendToChannel(
        channelName: String,
        message: Any,
    ): ZLinkKotlinMessageSendCall =
        JavaMessageSendCall(client.sendToChannel(channelName, message))

    override fun <TReply : Any> requestToChannel(
        channelName: String,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply> =
        JavaRequestCall(client.requestToChannel(channelName, request), replyType.java)
}

private class JavaFanoutClient(
    private val client: ZLinkFanoutClient,
) : ZLinkKotlinFanoutClient {
    override fun publish(
        channelName: String,
        topic: String,
        event: Any,
    ): ZLinkKotlinSubmissionCall =
        DeferredSubmissionCall {
            client.publish(channelName, topic, event).submit()
        }

    override fun publish(
        channelName: String,
        event: Any,
    ): ZLinkKotlinSubmissionCall =
        DeferredSubmissionCall {
            client.publish(channelName, event).submit()
        }
}

private class JavaRouteClient(
    private val client: ZLinkRouteClient,
) : ZLinkKotlinRouteClient {
    fun javaClient(): ZLinkRouteClient = client

    override fun sendToNode(
        meshName: String,
        target: RoutingId,
        message: Any,
    ): ZLinkKotlinMessageSendCall =
        JavaMessageSendCall(client.sendToNode(meshName, target, message))

    override fun <TReply : Any> requestToNode(
        meshName: String,
        target: RoutingId,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply> =
        JavaRequestCall(client.requestToNode(meshName, target, request), replyType.java)

    override fun sendToChannel(
        channelName: String,
        message: Any,
    ): ZLinkKotlinMessageSendCall =
        JavaMessageSendCall(client.sendToChannel(channelName, message))

    override fun <TReply : Any> requestToChannel(
        channelName: String,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply> =
        JavaRequestCall(client.requestToChannel(channelName, request), replyType.java)
}

private class JavaActorClient(
    private val client: ZLinkActorClient,
) : ZLinkKotlinActorClient {
    override fun sendToActor(
        actorId: String,
        message: Any,
    ): ZLinkKotlinMessageSendCall =
        DeferredMessageSendCall(
            submit = { metadata ->
                val call = metadata.entries.fold(client.sendToActor(actorId, message)) {
                        configured, entry ->
                    configured.metadata(entry.key, entry.value)
                }
                call.submit()
            },
        )

    override fun <TReply : Any> requestToActor(
        actorId: String,
        request: Any,
        replyType: KClass<TReply>,
    ): ZLinkKotlinRequestCall<TReply> {
        val call = object : ZLinkRequestCall {
            private var timeout: Duration? = null
            private val metadata = linkedMapOf<String, String>()

            override fun metadata(key: String, value: String): ZLinkRequestCall = apply {
                metadata[key] = value
            }

            override fun timeout(timeout: Duration): ZLinkRequestCall = apply {
                this.timeout = timeout
            }

            override fun <T> submit(type: Class<T>): CompletionStage<T> =
                client.requestToActor(actorId, request).let { initial ->
                    var requestCall = initial
                    metadata.forEach { (key, value) ->
                        requestCall = requestCall.metadata(key, value)
                    }
                    timeout?.let { requestCall = requestCall.timeout(it) }
                    requestCall.submit(type)
                }

            override fun <T> yield(type: Class<T>): CompletionStage<T> =
                client.requestToActor(actorId, request).let { initial ->
                    var requestCall = initial
                    metadata.forEach { (key, value) ->
                        requestCall = requestCall.metadata(key, value)
                    }
                    timeout?.let { requestCall = requestCall.timeout(it) }
                    requestCall.yield(type)
                }
        }
        return JavaRequestCall(call, replyType.java)
    }
}

private class JavaSessionClient(
    private val client: ZLinkSessionClient,
) : ZLinkKotlinSessionClient {
    override fun send(message: Any): ZLinkKotlinSessionSendCall =
        object : ZLinkKotlinSessionSendCall {
            private var call = client.send(message)

            override fun metadata(key: String, value: String): ZLinkKotlinSessionSendCall =
                apply { call = call.metadata(key, value) }

            override fun compress(): ZLinkKotlinSessionSendCall =
                apply { call = call.compress() }

            override suspend fun await() {
                awaitFrameworkStage(call.submit())
            }
        }

    override fun reply(message: Any): ZLinkKotlinSessionReplyCall =
        object : ZLinkKotlinSessionReplyCall {
            private var call = client.reply(message)

            override fun compress(): ZLinkKotlinSessionReplyCall =
                apply { call = call.compress() }

            override suspend fun await() {
                awaitFrameworkStage(call.submit())
            }
        }
}

fun ZLinkClient.kotlin(): ZLinkKotlinClient = JavaClient(this)

fun ZLinkFanoutClient.kotlin(): ZLinkKotlinFanoutClient = JavaFanoutClient(this)

fun ZLinkRouteClient.kotlin(): ZLinkKotlinRouteClient = JavaRouteClient(this)

fun ZLinkActorClient.kotlin(): ZLinkKotlinActorClient = JavaActorClient(this)

fun ZLinkSessionClient.kotlin(): ZLinkKotlinSessionClient = JavaSessionClient(this)

fun ZLinkSessionActor.kotlin(): ZLinkKotlinSessionActor =
    object : ZLinkKotlinSessionActor {
        override fun relay(
            message: systems.zlink.framework.messaging.ZLinkMessage,
        ): ZLinkKotlinSubmissionCall =
            DeferredSubmissionCall { this@kotlin.relay(message) }

        override fun relay(
            dispatch: systems.zlink.framework.streams.ZLinkSessionMessageContext,
            message: systems.zlink.framework.messaging.ZLinkMessage,
        ): ZLinkKotlinSubmissionCall =
            DeferredSubmissionCall { this@kotlin.relay(dispatch, message) }
    }

fun ZLinkBoundSession.kotlin(): ZLinkKotlinBoundSession =
    object : ZLinkKotlinBoundSession {
        override fun send(message: Any): ZLinkKotlinMessageSendCall =
            DeferredMessageSendCall(submit = { metadata ->
                val call = metadata.entries.fold(this@kotlin.send(message)) {
                        configured, entry ->
                    configured.metadata(entry.key, entry.value)
                }
                call.submit()
            })
    }

fun <T> ZLinkWorkerCall<T>.kotlin(): ZLinkKotlinWorkerCall<T> =
    object : ZLinkKotlinWorkerCall<T> {
        override suspend fun await(): T =
            awaitFrameworkStage(this@kotlin.submit())

        override suspend fun yield(): T =
            awaitFrameworkStage(this@kotlin.yield())
    }

inline fun <reified TReply : Any> ZLinkKotlinClient.requestToChannel(
    channelName: String,
    request: Any,
): ZLinkKotlinRequestCall<TReply> =
    requestToChannel(channelName, request, TReply::class)

inline fun <reified TReply : Any> ZLinkKotlinRouteClient.requestToNode(
    meshName: String,
    target: RoutingId,
    request: Any,
): ZLinkKotlinRequestCall<TReply> =
    requestToNode(meshName, target, request, TReply::class)

inline fun <reified TReply : Any> ZLinkKotlinRouteClient.requestToChannel(
    channelName: String,
    request: Any,
): ZLinkKotlinRequestCall<TReply> =
    requestToChannel(channelName, request, TReply::class)

inline fun <reified TReply : Any> ZLinkKotlinActorClient.requestToActor(
    actorId: String,
    request: Any,
): ZLinkKotlinRequestCall<TReply> =
    requestToActor(actorId, request, TReply::class)

fun ZLinkKotlinRouteClient.sendToSpot(
    spotId: String,
    message: Any,
): ZLinkKotlinSpotSendCall {
    val javaClient = (this as JavaRouteClient).javaClient()
    return JavaSpotSendCall(javaClient.sendToSpot(spotId, message))
}

inline fun <reified TReply : Any> ZLinkKotlinRouteClient.requestToSpot(
    spotId: String,
    request: Any,
): ZLinkKotlinSpotRequestCall<TReply> =
    requestToSpot(spotId, request, TReply::class)

fun <TReply : Any> ZLinkKotlinRouteClient.requestToSpot(
    spotId: String,
    request: Any,
    replyType: KClass<TReply>,
): ZLinkKotlinSpotRequestCall<TReply> {
    val javaClient = (this as JavaRouteClient).javaClient()
    return JavaSpotRequestCall(
        javaClient.requestToSpot(spotId, request),
        replyType.java,
    )
}
