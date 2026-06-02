package systems.zlink.samples.asynckotlin

import java.time.Duration
import java.util.Optional
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import kotlinx.coroutines.future.await
import kotlinx.coroutines.runBlocking
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestCall
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkSendCall
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.request
import systems.zlink.framework.kotlin.send

fun main() = runBlocking {
    val client = FakeClient()

    client.send("profile", Warmup("42"))
    val reply = client.request<ProfileReply, GetProfile>("profile", GetProfile("42"))

    require(reply.userId == "42") { "reply user id mismatch" }
    require(reply.status == "ready") { "reply status mismatch" }
    require(client.sent == 1) { "send was not submitted" }
    require(client.requests == 1) { "request was not submitted" }

    ZLinkCoroutineRuntime().use { runtime ->
        val handler = runtime.requestHandler<GetProfile, ProfileReply> { request, context ->
            require(context.packetName().orElseThrow() == "GetProfile") { "packet name mismatch" }
            ProfileReply(request.userId, "handled")
        }
        val handled = handler.handleAsync(GetProfile("42"), requestContext())
            .await()
        require(handled.status == "handled") { "handler reply mismatch" }
    }

    println("AsyncKotlin sample self-check passed")
}

private class FakeClient : ZLinkClient {
    var sent = 0
    var requests = 0

    override fun <TMessage> sendToChannel(channelName: String, message: TMessage): ZLinkSendCall {
        require(channelName == "profile") { "unexpected send channel" }
        require(message is Warmup) { "unexpected send message" }
        return object : ZLinkSendCall {
            override fun packetName(packetName: String): ZLinkSendCall = this
            override fun metadata(key: String, value: String): ZLinkSendCall = this
            override fun submitAsync(): CompletionStage<Void> {
                sent++
                return CompletableFuture.completedFuture(null)
            }
        }
    }

    override fun <TMessage> requestToChannel(channelName: String, message: TMessage): ZLinkRequestCall {
        require(channelName == "profile") { "unexpected request channel" }
        require(message is GetProfile) { "unexpected request message" }
        return object : ZLinkRequestCall {
            override fun packetName(packetName: String): ZLinkRequestCall = this
            override fun metadata(key: String, value: String): ZLinkRequestCall = this
            override fun timeout(timeout: Duration): ZLinkRequestCall = this
            override fun <TReply> submitAsync(replyType: Class<TReply>): CompletionStage<TReply> {
                requests++
                return CompletableFuture.completedFuture(replyType.cast(ProfileReply("42", "ready")))
            }
        }
    }
}

private fun requestContext(): ZLinkRequestContext =
    object : ZLinkRequestContext {
        override fun channelName(): Optional<String> = Optional.of("profile")
        override fun packetName(): Optional<String> = Optional.of("GetProfile")
        override fun contentType(): Optional<String> = Optional.empty()
        override fun cancellationToken(): CancellationToken = CancellationToken { false }
    }

private data class Warmup(val userId: String)
private data class GetProfile(val userId: String)
private data class ProfileReply(val userId: String, val status: String)
