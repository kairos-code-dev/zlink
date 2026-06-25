package systems.zlink.framework.kotlin

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder

suspend fun <TReply> ZLinkRequestCall.awaitReply(replyType: Class<TReply>): TReply =
    submit(replyType).await()

inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply =
    awaitReply(TReply::class.java)

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
