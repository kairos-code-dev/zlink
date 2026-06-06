package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import systems.zlink.framework.actors.ZLinkBoundSession
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkPublishCall
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkSendCall
import systems.zlink.framework.registry.ZLinkRegistryQuery
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry
import systems.zlink.framework.registry.ZLinkRegistryStatus
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry
import systems.zlink.framework.spots.ZLinkSpotCreateResult
import systems.zlink.framework.spots.ZLinkSpotInfo
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.streams.ZLinkSessionActors
import systems.zlink.framework.streams.ZLinkSessionReplyCall
import systems.zlink.framework.streams.ZLinkSessionSendCall
import java.util.Optional
import systems.zlink.contracts.core.RoutingId

suspend fun ZLinkSendCall.submit() {
    submitAsync().await()
}

suspend fun ZLinkPublishCall.submit() {
    submitAsync().await()
}

suspend fun <TReply> ZLinkRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    submitAsync(replyType).await()

inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

suspend fun ZLinkBoundSessionSendCall.submit() {
    submitAsync().await()
}

suspend fun ZLinkSessionSendCall.submit() {
    submitAsync().await()
}

suspend fun ZLinkSessionReplyCall.submit() {
    submitAsync().await()
}

suspend fun ZLinkBoundSession.disconnect() {
    disconnectAsync().await()
}

suspend fun <TMessage> ZLinkClient.send(
    channelName: String,
    message: TMessage,
) {
    sendToChannel(channelName, message).submit()
}

suspend inline fun <reified TReply, TMessage> ZLinkClient.request(
    channelName: String,
    message: TMessage,
): TReply =
    requestToChannel(channelName, message).awaitReply()

suspend fun <TMessage> ZLinkFanoutClient.publishToTopic(
    channelName: String,
    topic: String,
    message: TMessage,
) {
    publish(channelName, topic, message).submit()
}

suspend fun <TMessage> ZLinkRouteClient.send(
    channelName: String,
    target: RoutingId,
    message: TMessage,
) {
    sendTo(channelName, target, message).submit()
}

suspend inline fun <reified TReply, TMessage> ZLinkRouteClient.request(
    channelName: String,
    target: RoutingId,
    message: TMessage,
): TReply =
    requestTo(channelName, target, message).awaitReply()

suspend fun ZLinkActorManager.create(actorId: String, actorType: String): ZLinkActor =
    createAsync(actorId, actorType).await()

suspend fun ZLinkActorManager.find(actorId: String): Optional<ZLinkActor> =
    findAsync(actorId).await()

suspend fun ZLinkActorManager.getOrCreate(actorId: String, actorType: String): ZLinkActor =
    getOrCreateAsync(actorId, actorType).await()

suspend fun ZLinkSessionActors.bind(actor: ZLinkActor) =
    bindAsync(actor).await()

suspend fun ZLinkSessionActors.bind(actor: ZLinkActorRef) =
    bindAsync(actor).await()

suspend fun ZLinkSpotManager.create(spotType: Class<out ZLinkSpot>): ZLinkSpotCreateResult =
    createAsync(spotType).await()

suspend fun ZLinkSpotManager.create(spotType: Class<out ZLinkSpot>, spotRid: RoutingId): ZLinkSpotCreateResult =
    createAsync(spotType, spotRid).await()

suspend fun ZLinkSpotManager.getOrCreate(spotType: Class<out ZLinkSpot>, spotRid: RoutingId): ZLinkSpotCreateResult =
    getOrCreateAsync(spotType, spotRid).await()

suspend fun ZLinkSpotManager.find(spotRid: RoutingId): Optional<ZLinkSpotInfo> =
    findAsync(spotRid).await()

suspend fun ZLinkSpotManager.list(): List<ZLinkSpotInfo> =
    listAsync().await()

suspend fun ZLinkSpotManager.close(spotRid: RoutingId): Boolean =
    closeAsync(spotRid).await()

suspend fun ZLinkRegistryQuery.status(): ZLinkRegistryStatus =
    statusAsync().await()

suspend fun ZLinkRegistryQuery.serviceSummary(): List<ZLinkRegistryServiceSummaryEntry> =
    serviceSummaryAsync(null).await()

suspend fun ZLinkRegistryQuery.topology(): List<ZLinkRegistryTopologyEntry> =
    topologyAsync(null).await()
