package systems.zlink.framework.kotlin

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorDirectory
import systems.zlink.framework.actors.ZLinkActorClient
import systems.zlink.framework.actors.ZLinkActorRequestCall
import systems.zlink.framework.actors.ZLinkActorJoinCall
import systems.zlink.framework.actors.ZLinkActorJoinResult
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.actors.ActorRefSnapshot
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkFanoutPublishCall
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkSendCall
import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder
import systems.zlink.framework.locations.ZLinkLocationReadiness
import systems.zlink.framework.locations.ZLinkLocationRole
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotCreateResult
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spots.ZLinkWorkerCall
import systems.zlink.framework.spots.SpotHandle
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionActors
import kotlin.jvm.JvmName

suspend fun <TReply> ZLinkRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    awaitFrameworkStage(submit(replyType))

inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

suspend fun <TReply> ZLinkRequestCall.yieldReply(replyType: Class<TReply>): TReply =
    awaitFrameworkStage(yield(replyType))

inline suspend fun <reified TReply> ZLinkRequestCall.yieldReply(): TReply =
    yieldReply(TReply::class.java)

suspend fun <TReply> ZLinkActorRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    awaitFrameworkStage(submit(replyType))

inline suspend fun <reified TReply> ZLinkActorRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

suspend fun <TReply> ZLinkActorRequestCall.yieldReply(replyType: Class<TReply>): TReply =
    awaitFrameworkStage(yield(replyType))

inline suspend fun <reified TReply> ZLinkActorRequestCall.yieldReply(): TReply =
    yieldReply(TReply::class.java)

suspend fun <TReply> ZLinkActorClient.requestToActorAwait(
    actorRef: ActorRef,
    request: Any,
    replyType: Class<TReply>,
): TReply =
    requestToActor(actorRef, request).awaitReply(replyType)

inline suspend fun <reified TReply> ZLinkActorClient.requestToActorAwait(
    actorRef: ActorRef,
    request: Any,
): TReply =
    requestToActorAwait(actorRef, request, TReply::class.java)

suspend fun ZLinkActorDirectory.findActor(actorId: String): ActorRef? =
    awaitFrameworkStage(find(actorId)).orElse(null)

suspend fun ZLinkActorDirectory.ensureActor(
    actorId: String,
    createRequest: ZLinkMessage,
): ActorRef =
    awaitFrameworkStage(ensure(actorId, createRequest))

suspend fun ZLinkActorDirectory.ensureActor(
    actorId: String,
    createRequest: Any,
): ActorRef =
    awaitFrameworkStage(ensure(actorId, createRequest))

fun ActorRef.snapshot(): ActorRefSnapshot =
    ActorRefSnapshot.from(this)

fun ActorRefSnapshot.actorRef(): ActorRef =
    toActorRef()

suspend fun ZLinkLocationReadiness.isPeerReady(
    meshName: String,
    role: ZLinkLocationRole,
    nodeRid: RoutingId? = null,
): Boolean =
    awaitFrameworkStage(isPeerReady(meshName, role, nodeRid))

suspend fun ZLinkSessionActors.bindOrGetActor(actor: ActorRef): ZLinkSessionActor =
    awaitFrameworkStage(bindOrGet(actor))

@JvmName("awaitJoinCallVoid")
suspend fun ZLinkActorJoinCall.awaitJoin(): ZLinkActorJoinResult<Void> =
    awaitFrameworkStage(submit())

@JvmName("awaitJoinCall")
suspend fun <TReply> ZLinkActorJoinCall.awaitJoin(replyType: Class<TReply>): ZLinkActorJoinResult<TReply> =
    awaitFrameworkStage(submit(replyType))

@JvmName("awaitJoinCallReified")
inline suspend fun <reified TReply> ZLinkActorJoinCall.awaitJoinReply(): ZLinkActorJoinResult<TReply> =
    awaitJoin(TReply::class.java)

@JvmName("yieldJoinCallVoid")
suspend fun ZLinkActorJoinCall.yieldJoin(): ZLinkActorJoinResult<Void> =
    awaitFrameworkStage(yield())

@JvmName("yieldJoinCall")
suspend fun <TReply> ZLinkActorJoinCall.yieldJoin(replyType: Class<TReply>): ZLinkActorJoinResult<TReply> =
    awaitFrameworkStage(yield(replyType))

@JvmName("yieldJoinCallReified")
inline suspend fun <reified TReply> ZLinkActorJoinCall.yieldJoinReply(): ZLinkActorJoinResult<TReply> =
    yieldJoin(TReply::class.java)

suspend fun <T> ZLinkWorkerCall<T>.yieldWorker(): T =
    awaitFrameworkStage(yield())

fun <TMessage> ZLinkClient.send(
    channelName: String,
    message: TMessage,
): ZLinkSendCall = sendToChannel(channelName, message)

suspend inline fun <reified TReply> ZLinkClient.request(
    channelName: String,
    message: Message,
): TReply =
    requestToChannel(channelName, message).awaitReply()

fun <TEvent> ZLinkFanoutClient.publishToTopic(
    channelName: String,
    message: TEvent,
): ZLinkFanoutPublishCall = publish(channelName, message)

fun <TMessage> ZLinkRouteClient.send(
    meshName: String,
    target: RoutingId,
    message: TMessage,
): ZLinkSendCall = sendToNode(meshName, target, message)

fun <TMessage> ZLinkRouteClient.send(
    channelName: String,
    message: TMessage,
): ZLinkSendCall = sendToChannel(channelName, message)

suspend inline fun <reified TReply> ZLinkRouteClient.request(
    channelName: String,
    target: RoutingId,
    message: Message,
): TReply =
    requestToNode(channelName, target, message).awaitReply()

fun <TMessage> ZLinkRouteClient.send(
    target: SpotHandle,
    message: TMessage,
): ZLinkSendCall = sendToSpot(target, message)

suspend inline fun <reified TReply> ZLinkRouteClient.request(
    target: SpotHandle,
    message: Message,
): TReply =
    requestToSpot(target, message).awaitReply()

fun ZLinkFrameworkOptions.configureStreamCompression(
    configure: ZLinkStreamCompressionBuilder.() -> Unit,
): ZLinkFrameworkOptions {
    configureStreamCompression().configure()
    return this
}
