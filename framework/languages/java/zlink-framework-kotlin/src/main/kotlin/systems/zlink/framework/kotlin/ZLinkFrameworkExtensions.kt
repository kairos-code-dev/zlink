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

@JvmName("yieldAwaitRequestCall")
suspend fun <TReply> yieldAwait(call: ZLinkRequestCall, replyType: Class<TReply>): TReply =
    call.yieldAsync(replyType).await()

@JvmName("yieldAwaitRequestCallReified")
inline suspend fun <reified TReply> yieldAwait(call: ZLinkRequestCall): TReply =
    yieldAwait(call, TReply::class.java)

@JvmName("yieldAwaitJoinSpotCallVoid")
suspend fun yieldAwait(call: ZLinkActorJoinSpotCall): ZLinkActorJoinResult<Void> =
    call.yieldAsync().await()

@JvmName("yieldAwaitJoinSpotCall")
suspend fun <TReply> yieldAwait(
    call: ZLinkActorJoinSpotCall,
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply> =
    call.yieldAsync(replyType).await()

@JvmName("yieldAwaitJoinSpotCallReified")
inline suspend fun <reified TReply> yieldAwait(
    call: ZLinkActorJoinSpotCall,
): ZLinkActorJoinResult<TReply> =
    yieldAwait(call, TReply::class.java)

@JvmName("yieldAwaitJoinEntrySpotCallVoid")
suspend fun yieldAwait(call: ZLinkActorJoinEntrySpotCall): ZLinkActorJoinResult<Void> =
    call.yieldAsync().await()

@JvmName("yieldAwaitJoinEntrySpotCall")
suspend fun <TReply> yieldAwait(
    call: ZLinkActorJoinEntrySpotCall,
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply> =
    call.yieldAsync(replyType).await()

@JvmName("yieldAwaitJoinEntrySpotCallReified")
inline suspend fun <reified TReply> yieldAwait(
    call: ZLinkActorJoinEntrySpotCall,
): ZLinkActorJoinResult<TReply> =
    yieldAwait(call, TReply::class.java)

@JvmName("yieldAwaitBoundSessionSendCall")
suspend fun yieldAwait(call: ZLinkBoundSessionSendCall) {
    call.yieldAsync().await()
}

@JvmName("yieldAwaitWorkerCall")
suspend fun <T> yieldAwait(call: ZLinkWorkerCall<T>): T =
    call.yieldAsync().await()

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
