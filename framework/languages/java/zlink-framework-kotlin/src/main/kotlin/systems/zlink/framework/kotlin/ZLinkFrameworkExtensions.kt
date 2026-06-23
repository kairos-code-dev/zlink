package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient

suspend fun <TReply> ZLinkRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    submit(replyType).await()

inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

suspend fun ZLinkClient.send(
    channelName: String,
    message: Any,
) {
    sendToChannel(channelName, message).submit().await()
}

suspend inline fun <reified TReply> ZLinkClient.request(
    channelName: String,
    message: Any,
): TReply =
    requestToChannel(channelName, message).awaitReply()

suspend fun ZLinkFanoutClient.publishToTopic(
    channelName: String,
    topic: String,
    message: Any,
) {
    publish(channelName, topic, message).submit().await()
}

suspend fun ZLinkRouteClient.send(
    channelName: String,
    target: RoutingId,
    message: Any,
) {
    sendTo(channelName, target, message).submit().await()
}

suspend inline fun <reified TReply> ZLinkRouteClient.request(
    channelName: String,
    target: RoutingId,
    message: Any,
): TReply =
    requestTo(channelName, target, message).awaitReply()
