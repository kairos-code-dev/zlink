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

suspend fun <TMessage> ZLinkClient.send(
    channelName: String,
    message: TMessage,
) {
    sendToChannel(channelName, message).submit().await()
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
    publish(channelName, topic, message).submit().await()
}

suspend fun <TMessage> ZLinkRouteClient.send(
    channelName: String,
    target: RoutingId,
    message: TMessage,
) {
    sendTo(channelName, target, message).submit().await()
}

suspend inline fun <reified TReply, TMessage> ZLinkRouteClient.request(
    channelName: String,
    target: RoutingId,
    message: TMessage,
): TReply =
    requestTo(channelName, target, message).awaitReply()
