package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorDirectory
import systems.zlink.framework.actors.ZLinkActorClient
import systems.zlink.framework.actors.ZLinkActorRequestCall
import systems.zlink.framework.actors.ZLinkActorSendCall
import systems.zlink.framework.actors.ZLinkActorJoinCall
import systems.zlink.framework.actors.ZLinkActorJoinEntrySpotCall
import systems.zlink.framework.actors.ZLinkActorJoinResult
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall
import systems.zlink.framework.actors.ZLinkActorPlacement
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.actors.ZLinkActorRefSnapshot
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkYieldRequestCall
import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder
import systems.zlink.framework.locations.ZLinkLocationReadiness
import systems.zlink.framework.locations.ZLinkLocationRole
import systems.zlink.framework.locations.ZLinkSpotAddress
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotCreateResult
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spots.ZLinkWorkerCall
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionActors
import kotlin.jvm.JvmName

suspend fun <TReply> ZLinkRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    submit(replyType).await()

inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

suspend fun ZLinkActorSendCall.awaitSend() {
    submit().await()
}

suspend fun <TReply> ZLinkActorRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    submit(replyType).await()

inline suspend fun <reified TReply> ZLinkActorRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

suspend fun ZLinkActorClient.sendToActorAwait(
    actorId: String,
    message: Any,
) {
    sendToActor(actorId, message).awaitSend()
}

suspend fun <TReply> ZLinkActorClient.requestToActorAwait(
    actorId: String,
    request: Any,
    replyType: Class<TReply>,
): TReply =
    requestToActor(actorId, request).awaitReply(replyType)

inline suspend fun <reified TReply> ZLinkActorClient.requestToActorAwait(
    actorId: String,
    request: Any,
): TReply =
    requestToActorAwait(actorId, request, TReply::class.java)

suspend fun ZLinkActorDirectory.findActor(actorId: String): ZLinkActorRef? =
    find(actorId).await().orElse(null)

suspend fun ZLinkActorDirectory.ensureActor(
    actorId: String,
    createRequest: ZLinkMessage,
    placement: ZLinkActorPlacement = ZLinkActorPlacement.any(),
): ZLinkActorRef =
    ensure(actorId, createRequest, placement).await()

suspend fun ZLinkActorDirectory.ensureActor(
    actorId: String,
    createRequest: Any,
): ZLinkActorRef =
    ensure(actorId, createRequest).await()

fun ZLinkActorRef.snapshot(): ZLinkActorRefSnapshot =
    ZLinkActorRefSnapshot.from(this)

fun ZLinkActorRefSnapshot.actorRef(): ZLinkActorRef =
    toActorRef()

suspend fun ZLinkLocationReadiness.isPeerReady(
    meshName: String,
    role: ZLinkLocationRole,
    nodeRid: RoutingId? = null,
): Boolean =
    isPeerReadyAsync(meshName, role, nodeRid).await()

suspend fun ZLinkSessionActors.bindOrGetActor(actor: ZLinkActorRef): ZLinkSessionActor =
    bindOrGet(actor).await()

@JvmName("awaitJoinCallVoid")
suspend fun ZLinkActorJoinCall.awaitJoin(): ZLinkActorJoinResult<Void> =
    submit().await()

@JvmName("awaitJoinCall")
suspend fun <TReply> ZLinkActorJoinCall.awaitJoin(replyType: Class<TReply>): ZLinkActorJoinResult<TReply> =
    submit(replyType).await()

@JvmName("awaitJoinCallReified")
inline suspend fun <reified TReply> ZLinkActorJoinCall.awaitJoin(): ZLinkActorJoinResult<TReply> =
    awaitJoin(TReply::class.java)

@JvmName("yieldRequestCall")
suspend fun <TReply> yield(call: ZLinkYieldRequestCall, replyType: Class<TReply>): TReply =
    call.yield(replyType)

@JvmName("yieldRequestCallReified")
inline suspend fun <reified TReply> yield(call: ZLinkYieldRequestCall): TReply =
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

@JvmName("yieldWorkerCall")
suspend fun <T> yield(call: ZLinkWorkerCall<T>): T =
    call.yield()

fun ZLinkClient.send(
    channelName: String,
    message: Message,
) {
    sendToChannel(channelName, message).submit()
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
    publish(channelName, topic, message).submit()
}

fun ZLinkRouteClient.send(
    channelName: String,
    target: RoutingId,
    message: Message,
) {
    sendToNode(channelName, target, message).submit()
}

suspend inline fun <reified TReply> ZLinkRouteClient.request(
    channelName: String,
    target: RoutingId,
    message: Message,
): TReply =
    requestToNode(channelName, target, message).awaitReply()

fun ZLinkRouteClient.send(
    channelName: String,
    address: ZLinkSpotAddress,
    message: Message,
) {
    sendToSpot(channelName, address, message).submit()
}

suspend inline fun <reified TReply> ZLinkRouteClient.request(
    channelName: String,
    address: ZLinkSpotAddress,
    message: Message,
): TReply =
    requestToSpot(channelName, address, message).awaitReply()

suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(): ZLinkSpotCreateResult =
    create(TSpot::class.java).await()

suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    request: ZLinkMessage,
): ZLinkSpotCreateResult =
    create(TSpot::class.java, request).await()

suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    spotRid: RoutingId,
): ZLinkSpotCreateResult =
    create(TSpot::class.java, spotRid).await()

suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
    spotRid: RoutingId,
): ZLinkSpotCreateResult =
    getOrCreate(TSpot::class.java, spotRid).await()

suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
    spotRid: RoutingId,
    request: ZLinkMessage,
): ZLinkSpotCreateResult =
    getOrCreate(TSpot::class.java, spotRid, request).await()

fun ZLinkFrameworkOptions.configureStreamCompression(
    configure: ZLinkStreamCompressionBuilder.() -> Unit,
): ZLinkFrameworkOptions {
    configureStreamCompression().configure()
    return this
}
