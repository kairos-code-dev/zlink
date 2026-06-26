package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorJoinEntrySpotCall
import systems.zlink.framework.actors.ZLinkActorJoinResult
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkRouteRequestCall
import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder
import systems.zlink.framework.spots.ZLinkWorkerCall
import kotlin.jvm.JvmName

suspend fun <TReply> ZLinkRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    submit(replyType).await()

inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

suspend fun <TReply> ZLinkRouteRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    submit(replyType).await()

inline suspend fun <reified TReply> ZLinkRouteRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

@JvmName("yieldRequestCall")
suspend fun <TReply> yield(call: ZLinkRequestCall, replyType: Class<TReply>): TReply =
    call.yield(replyType)

@JvmName("yieldRequestCallReified")
inline suspend fun <reified TReply> yield(call: ZLinkRequestCall): TReply =
    yield(call, TReply::class.java)

@JvmName("yieldJoinSpotCallVoid")
suspend fun yield(call: ZLinkActorJoinSpotCall): ZLinkActorJoinResult<Void> =
    call.yield()

@JvmName("yieldJoinSpotCall")
suspend fun <TReply> yield(
    call: ZLinkActorJoinSpotCall,
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply> =
    call.yield(replyType)

@JvmName("yieldJoinSpotCallReified")
inline suspend fun <reified TReply> yield(
    call: ZLinkActorJoinSpotCall,
): ZLinkActorJoinResult<TReply> =
    yield(call, TReply::class.java)

@JvmName("yieldJoinEntrySpotCallVoid")
suspend fun yield(call: ZLinkActorJoinEntrySpotCall): ZLinkActorJoinResult<Void> =
    call.yield()

@JvmName("yieldJoinEntrySpotCall")
suspend fun <TReply> yield(
    call: ZLinkActorJoinEntrySpotCall,
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply> =
    call.yield(replyType)

@JvmName("yieldJoinEntrySpotCallReified")
inline suspend fun <reified TReply> yield(
    call: ZLinkActorJoinEntrySpotCall,
): ZLinkActorJoinResult<TReply> =
    yield(call, TReply::class.java)

@JvmName("yieldBoundSessionSendCall")
suspend fun yield(call: ZLinkBoundSessionSendCall) {
    call.yield()
}

@JvmName("yieldWorkerCall")
suspend fun <T> yield(call: ZLinkWorkerCall<T>): T =
    call.yield()

suspend fun ZLinkClient.send(
    channelName: String,
    message: Message,
) {
    sendToChannel(channelName, message).submit().await()
}

suspend inline fun <reified TReply> ZLinkClient.request(
    channelName: String,
    message: Message,
): TReply =
    requestToChannel(channelName, message).awaitReply()

suspend fun ZLinkFanoutClient.publishToTopic(
    channelName: String,
    topic: String,
    message: Message,
) {
    publish(channelName, topic, message).submit().await()
}

suspend fun ZLinkRouteClient.send(
    channelName: String,
    target: RoutingId,
    message: Message,
) {
    sendTo(channelName, target, message).submit().await()
}

suspend inline fun <reified TReply> ZLinkRouteClient.request(
    channelName: String,
    target: RoutingId,
    message: Message,
): TReply =
    requestTo(channelName, target, message).awaitReply()

fun ZLinkFrameworkOptions.configureStreamCompression(
    configure: ZLinkStreamCompressionBuilder.() -> Unit,
): ZLinkFrameworkOptions {
    configureStreamCompression().configure()
    return this
}
