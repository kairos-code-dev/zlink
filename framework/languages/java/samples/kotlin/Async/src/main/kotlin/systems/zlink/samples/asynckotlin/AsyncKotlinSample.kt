package systems.zlink.samples.asynckotlin

import java.time.Duration
import java.util.Optional
import java.util.UUID
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.future.await
import kotlinx.coroutines.runBlocking
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkFramework
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.framework.kotlin.submit

private val warmupReceived = CountDownLatch(1)
private val warmupUser = AtomicReference<String>()

fun main() = runBlocking {
    val endpoint = "inproc://zlink-kotlin-async-${UUID.randomUUID()}"

    ZLinkFramework.start { options ->
        options.addClientServerChannel("profile") { channel ->
            channel.enableServer { server -> server.bind(endpoint) }
            channel.enableClient { client ->
                client.useManualConnections { endpoints -> endpoints.connect(endpoint) }
            }
            channel.addSendHandler(WarmupHandler::class.java, String::class.java, "Warmup")
            channel.addRequestHandler(GetProfileHandler::class.java, String::class.java, String::class.java, "GetProfile")
        }
    }.use { framework ->
        framework.client()
            .sendToChannel("profile", "42")
            .packetName("Warmup")
            .metadata("sample", "kotlin")
            .submit()

        val reply = framework.client()
            .requestToChannel("profile", "42")
            .packetName("GetProfile")
            .timeout(Duration.ofSeconds(1))
            .awaitReply<String>()

        require(warmupReceived.await(1, TimeUnit.SECONDS)) { "warmup send was not delivered" }
        require(warmupUser.get() == "42") { "warmup user id mismatch" }
        require(reply == "42:ready") { "reply mismatch" }
    }

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

class WarmupHandler : ZLinkSendHandler<String> {
    override fun handleAsync(message: String, context: ZLinkSendContext): CompletionStage<Void> {
        require(context.packetName().orElseThrow() == "Warmup") { "send packet mismatch" }
        warmupUser.set(message)
        warmupReceived.countDown()
        return CompletableFuture.completedFuture(null)
    }
}

class GetProfileHandler : ZLinkRequestHandler<String, String> {
    override fun handleAsync(request: String, context: ZLinkRequestContext): CompletionStage<String> {
        require(context.packetName().orElseThrow() == "GetProfile") { "request packet mismatch" }
        return CompletableFuture.completedFuture("$request:ready")
    }
}

private fun requestContext(): ZLinkRequestContext =
    object : ZLinkRequestContext {
        override fun channelName(): Optional<String> = Optional.of("profile")
        override fun packetName(): Optional<String> = Optional.of("GetProfile")
        override fun contentType(): Optional<String> = Optional.empty()
        override fun cancellationToken(): CancellationToken = CancellationToken { false }
    }

private data class GetProfile(val userId: String)
private data class ProfileReply(val userId: String, val status: String)
