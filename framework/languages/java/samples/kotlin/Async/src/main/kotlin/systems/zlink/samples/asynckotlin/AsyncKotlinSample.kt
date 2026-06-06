package systems.zlink.samples.asynckotlin

import java.time.Duration
import java.util.Optional
import java.util.UUID
import java.util.concurrent.CompletableFuture
import kotlinx.coroutines.future.await
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.springframework.context.annotation.AnnotationConfigApplicationContext
import org.springframework.context.annotation.Bean
import org.springframework.context.annotation.Configuration
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.handlers.ZLinkSend
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.framework.kotlin.submit
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private val warmupUser = CompletableFuture<String>()

fun main() = runBlocking {
    val endpoint = "inproc://zlink-kotlin-async-${UUID.randomUUID()}"

    AnnotationConfigApplicationContext().use { context ->
        context.registerBean(
            "asyncEndpoint",
            String::class.java,
            java.util.function.Supplier { endpoint },
        )
        context.register(AsyncKotlinSpringConfig::class.java)
        context.refresh()

        val client = context.getBean(ZLinkClient::class.java)
        client
            .sendToChannel("profile", "42")
            .packetName("Warmup")
            .metadata("sample", "kotlin")
            .submit()

        val reply = client
            .requestToChannel("profile", "42")
            .packetName("GetProfile")
            .timeout(Duration.ofSeconds(1))
            .awaitReply<String>()

        require(withTimeout(1_000) { warmupUser.await() } == "42") { "warmup user id mismatch" }
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

@Configuration(proxyBeanMethods = false)
@EnableZLinkFramework
open class AsyncKotlinSpringConfig {
    @Bean
    open fun asyncHandlers() = AsyncHandlers()

    @Bean
    open fun zlinkFramework(asyncEndpoint: String) = ZLinkFrameworkConfigurer { options ->
        options.addHandlersFromPackageOf(AsyncKotlinSpringConfig::class.java)
        options.addClientServerChannel("profile") { channel ->
            channel.enableServer { server -> server.bind(asyncEndpoint) }
            channel.enableClient { client ->
                client.useManualConnections { endpoints -> endpoints.connect(asyncEndpoint) }
            }
            channel.addHandlerGroup("async-kotlin")
        }
    }
}

@ZLinkHandlerGroup("async-kotlin")
class AsyncHandlers {
    @ZLinkSend(packetName = "Warmup")
    suspend fun warmup(message: String, context: ZLinkSendContext) {
        require(context.packetName().orElseThrow() == "Warmup") { "send packet mismatch" }
        warmupUser.complete(message)
    }

    @ZLinkRequest(packetName = "GetProfile")
    suspend fun getProfile(request: String, context: ZLinkRequestContext): String {
        require(context.packetName().orElseThrow() == "GetProfile") { "request packet mismatch" }
        return "$request:ready"
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
