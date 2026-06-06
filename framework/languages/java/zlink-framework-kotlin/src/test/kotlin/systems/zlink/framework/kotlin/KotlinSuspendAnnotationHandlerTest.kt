package systems.zlink.framework.kotlin

import java.time.Duration
import java.util.concurrent.CancellationException
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.coroutines.coroutineContext
import kotlinx.coroutines.Job
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertSame
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import org.springframework.context.annotation.AnnotationConfigApplicationContext
import org.springframework.context.annotation.Bean
import org.springframework.context.annotation.Configuration
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.errors.ZLinkConfigurationException
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkPacket
import systems.zlink.framework.handlers.ZLinkPublish
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.handlers.ZLinkSend
import systems.zlink.framework.handlers.ZLinkSpotActorDisconnected
import systems.zlink.framework.handlers.ZLinkSpotActorJoin
import systems.zlink.framework.handlers.ZLinkSpotActorLeft
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.handlers.ZLinkSpotActorSend
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerSurface
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkAutoConfiguration
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotActorChangeKind
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory

final class KotlinSuspendAnnotationHandlerTest {
    @Test
    fun scannerTreatsKotlinSuspendChannelAnnotationsLikeJavaMethodHandlers() {
        val catalog = ZLinkHandlerScanner.scan(setOf(KotlinSuspendHandlerMarker::class.java))

        val request = catalog.matching(
            setOf("kotlin-channel"),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.REQUEST,
        ).single()
        val send = catalog.matching(
            setOf("kotlin-channel"),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.SEND,
        ).single()
        val publish = catalog.matching(
            setOf("kotlin-fanout"),
            ZLinkScannedHandlerSurface.CHANNEL,
            ZLinkScannedHandlerKind.PUBLISH,
        ).single()

        assertEquals(ProfileRequest::class.java, request.messageType())
        assertEquals(ProfileReply::class.java, request.replyType())
        assertEquals(ProfileGreeting::class.java, send.messageType())
        assertEquals(ProfileEvent::class.java, publish.messageType())
        assertTrue(ZLinkHandlerMethodInvoker.isKotlinSuspendMethod(request.handlerMethod()))
    }

    @Test
    fun scannerTreatsKotlinSuspendSpotActorAnnotationsLikeJavaMethodHandlers() {
        val catalog = ZLinkHandlerScanner.scan(setOf(KotlinSuspendHandlerMarker::class.java))

        val join = catalog.matching(
            setOf("kotlin-spot"),
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.ACTOR_JOIN,
        ).single()
        val request = catalog.matching(
            setOf("kotlin-spot"),
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.ACTOR_REQUEST,
        ).single()
        val send = catalog.matching(
            setOf("kotlin-spot"),
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.ACTOR_SEND,
        ).single()
        val joined = catalog.matching(
            setOf("kotlin-spot"),
            ZLinkScannedHandlerSurface.SPOT,
            ZLinkScannedHandlerKind.ACTOR_JOINED,
        ).single()

        assertEquals(JoinRequest::class.java, join.messageType())
        assertEquals(JoinReply::class.java, join.replyType())
        assertEquals(PlayerCommand::class.java, request.messageType())
        assertEquals(PlayerReply::class.java, request.replyType())
        assertEquals(PlayerEvent::class.java, send.messageType())
        assertEquals(PlayerActor::class.java, joined.messageType())
        assertTrue(ZLinkHandlerMethodInvoker.isKotlinSuspendMethod(request.handlerMethod()))
    }

    @Test
    fun kotlinSuspendSpotActorMethodRunsThroughMethodInvoker() {
        val handler = KotlinSpringSuspendSpotActorHandler()
        val method = KotlinSpringSuspendSpotActorHandler::class.java.methods.single {
            it.name == "request"
        }

        val reply = ZLinkHandlerMethodInvoker
            .invoke(
                handler,
                method,
                arrayOf(PlayerActor("p1"), PlayerCommand("move")),
            )
            .toCompletableFuture()
            .get(1, TimeUnit.SECONDS)

        assertEquals(PlayerReply("p1:move"), reply)
    }

    @Test
    fun kotlinSuspendAnnotationRunsInsideFrameworkCoroutineContext() {
        val handler = KotlinCoroutineContextHandler()
        val method = KotlinCoroutineContextHandler::class.java.methods.single {
            it.name == "request"
        }

        val reply = ZLinkHandlerMethodInvoker
            .invoke(
                handler,
                method,
                arrayOf(ProfileRequest("Ada")),
            )
            .toCompletableFuture()
            .get(1, TimeUnit.SECONDS)

        assertEquals(ProfileReply("coroutine:Ada"), reply)
    }

    @Test
    fun springCreatedKotlinSuspendHandlerRunsThroughMethodInvoker() {
        AnnotationConfigApplicationContext().use { context ->
            context.register(SpringSuspendHandlerConfig::class.java)
            context.refresh()
            val handler = context.getBean(KotlinSpringSuspendChannelHandler::class.java)
            val method = KotlinSpringSuspendChannelHandler::class.java.methods.single {
                it.name == "request"
            }

            val reply = ZLinkHandlerMethodInvoker
                .invoke(
                    handler,
                    method,
                    arrayOf(ProfileRequest("Ada"), requestContext("profile")),
                )
                .toCompletableFuture()
                .get(1, TimeUnit.SECONDS)

            assertEquals(ProfileReply("profile:Ada:injected"), reply)
            assertSame(handler, context.getBean(KotlinSpringSuspendChannelHandler::class.java))
        }
    }

    @Test
    fun kotlinSuspendAnnotationExceptionCompletesJavaStageExceptionally() {
        val handler = KotlinSuspendFailureHandler()
        val method = KotlinSuspendFailureHandler::class.java.methods.single {
            it.name == "fail"
        }

        val failure = assertThrows<ExecutionException> {
            ZLinkHandlerMethodInvoker
                .invoke(handler, method, arrayOf(ProfileRequest("Ada")))
                .toCompletableFuture()
                .get(1, TimeUnit.SECONDS)
        }

        assertTrue(
            failure.cause is IllegalStateException,
            "unexpected cause: ${failure.cause?.javaClass?.name}: ${failure.cause?.message}",
        )
        assertEquals("boom:Ada", failure.cause!!.message)
    }

    @Test
    fun kotlinSuspendAnnotationCancellationCompletesJavaStageExceptionally() {
        val handler = KotlinSuspendFailureHandler()
        val method = KotlinSuspendFailureHandler::class.java.methods.single {
            it.name == "cancel"
        }

        val failure = assertThrows<CancellationException> {
            ZLinkHandlerMethodInvoker
                .invoke(handler, method, arrayOf(ProfileRequest("Ada")))
                .toCompletableFuture()
                .get(1, TimeUnit.SECONDS)
        }

        assertEquals("cancel:Ada", failure.message)
    }

    @Test
    fun duplicateValidationRejectsJavaAndKotlinSuspendAnnotationPacketCollision() {
        val options = DefaultZLinkFrameworkOptions()
        options.addHandlersFromPackageOf(KotlinSuspendHandlerMarker::class.java)
        options.addClientServerChannel("profile") { channel ->
            channel.enableServer { server -> server.bind("inproc://profile") }
            channel.addHandlerGroup("kotlin-channel")
            channel.addRequestHandler(
                JavaProfileRequestHandler::class.java,
                ProfileRequest::class.java,
                ProfileReply::class.java,
                "ProfileRequest",
            )
        }

        val failure = assertThrows<ZLinkConfigurationException> {
            ZLinkFrameworkRuntime.start(options, FakeZLinkBackendAdapterFactory()).close()
        }

        assertTrue(failure.message!!.contains("duplicate client/server request handler packet name"))
    }

    @Test
    fun springLifecycleDiscoversKotlinSuspendAnnotationBeanType() {
        AnnotationConfigApplicationContext().use { context ->
            val backendFactory = FakeZLinkBackendAdapterFactory()
            context.registerBean(
                ZLinkBackendAdapterFactory::class.java,
                java.util.function.Supplier { backendFactory },
            )
            context.register(
                SpringSuspendFrameworkConfig::class.java,
                ZLinkFrameworkAutoConfiguration::class.java,
            )
            context.refresh()

            assertTrue(
                backendFactory.calls().contains("router.bind.inproc://kotlin-suspend"),
                backendFactory.calls().toString(),
            )
        }
    }

    private fun requestContext(channelName: String) =
        object : ZLinkRequestContext {
            override fun channelName() = java.util.Optional.of(channelName)
            override fun packetName() = java.util.Optional.of("ProfileRequest")
            override fun contentType() = java.util.Optional.empty<String>()
            override fun cancellationToken() = systems.zlink.framework.CancellationToken { false }
        }
}

class KotlinSuspendHandlerMarker

class KotlinCoroutineContextHandler {
    @ZLinkRequest
    suspend fun request(request: ProfileRequest): ProfileReply =
        ProfileReply(if (coroutineContext[Job] != null) "coroutine:${request.name}" else "missing")
}

@ZLinkHandlerGroup("kotlin-channel")
class KotlinSpringSuspendChannelHandler(private val dependency: SuspendDependency) {
    @ZLinkRequest
    suspend fun request(request: ProfileRequest, context: ZLinkRequestContext): ProfileReply =
        ProfileReply("${context.channelName().orElse("missing")}:${request.name}:${dependency.value}")

    @ZLinkSend
    suspend fun send(message: ProfileGreeting, context: ZLinkSendContext) {
        ObservedValues.lastSend.set("${context.packetName().orElse("missing")}:${message.value}")
    }
}

@ZLinkHandlerGroup("kotlin-fanout")
class KotlinSpringSuspendPublishHandler {
    @ZLinkPublish
    suspend fun publish(message: ProfileEvent, context: ZLinkPublishContext) {
        ObservedValues.lastPublish.set("${context.topic()}:${message.value}")
    }
}

@ZLinkHandlerGroup("kotlin-spot")
class KotlinSpringSuspendSpotActorHandler {
    @ZLinkSpotActorJoin
    suspend fun join(actor: PlayerActor, request: JoinRequest): JoinReply =
        JoinReply("${actor.actorId()}:${request.value}")

    @ZLinkSpotActorRequest
    suspend fun request(actor: PlayerActor, request: PlayerCommand): PlayerReply =
        PlayerReply("${actor.actorId()}:${request.value}")

    @ZLinkSpotActorSend
    suspend fun send(actor: PlayerActor, message: PlayerEvent) {
        ObservedValues.lastActorSend.set("${actor.actorId()}:${message.value}")
    }

    @ZLinkSpotPostActorJoined
    suspend fun joined(actor: PlayerActor, result: ZLinkSpotActorChangeResult) {
        ObservedValues.lastActorJoined.set("${actor.actorId()}:${result.kind()}")
    }

    @ZLinkSpotActorLeft
    suspend fun left(actor: PlayerActor, result: ZLinkSpotActorChangeResult) {
        ObservedValues.lastActorLeft.set("${actor.actorId()}:${result.kind()}")
    }

    @ZLinkSpotActorDisconnected
    suspend fun disconnected(actor: PlayerActor) {
        ObservedValues.lastActorDisconnected.set(actor.actorId())
    }
}

@ZLinkHandlerGroup("kotlin-failure")
class KotlinSuspendFailureHandler {
    @ZLinkRequest(packetName = "FailProfile")
    suspend fun fail(request: ProfileRequest): ProfileReply {
        throw IllegalStateException("boom:${request.name}")
    }

    @ZLinkRequest(packetName = "CancelProfile")
    suspend fun cancel(request: ProfileRequest): ProfileReply {
        throw CancellationException("cancel:${request.name}")
    }
}

class JavaProfileRequestHandler : systems.zlink.framework.channels.ZLinkRequestHandler<ProfileRequest, ProfileReply> {
    override fun handleAsync(
        request: ProfileRequest,
        context: ZLinkRequestContext,
    ) = CompletableFuture.completedFuture(ProfileReply(request.name))
}

@Configuration(proxyBeanMethods = false)
open class SpringSuspendHandlerConfig {
    @Bean
    open fun dependency() = SuspendDependency("injected")

    @Bean
    open fun handler(dependency: SuspendDependency) = KotlinSpringSuspendChannelHandler(dependency)
}

@Configuration(proxyBeanMethods = false)
@EnableZLinkFramework
open class SpringSuspendFrameworkConfig {
    @Bean
    open fun dependency() = SuspendDependency("injected")

    @Bean
    open fun handler(dependency: SuspendDependency) = KotlinSpringSuspendChannelHandler(dependency)

    @Bean
    open fun frameworkConfigurer() = ZLinkFrameworkConfigurer { options ->
        options.setDefaultTimeout(Duration.ofSeconds(1))
        options.addHandlersFromPackageOf(KotlinSuspendHandlerMarker::class.java)
        options.addClientServerChannel("profile") { channel ->
            channel.enableServer { server -> server.bind("inproc://kotlin-suspend") }
            channel.addHandlerGroup("kotlin-channel")
        }
    }
}

data class SuspendDependency(val value: String)

object ObservedValues {
    val lastSend: AtomicReference<String> = AtomicReference()
    val lastPublish: AtomicReference<String> = AtomicReference()
    val lastActorSend: AtomicReference<String> = AtomicReference()
    val lastActorJoined: AtomicReference<String> = AtomicReference()
    val lastActorLeft: AtomicReference<String> = AtomicReference()
    val lastActorDisconnected: AtomicReference<String> = AtomicReference()
}

@ZLinkPacket("ProfileRequest")
data class ProfileRequest(val name: String)

data class ProfileReply(val value: String)

@ZLinkPacket("ProfileGreeting")
data class ProfileGreeting(val value: String)

@ZLinkPacket("ProfileEvent")
data class ProfileEvent(val value: String)

class PlayerActor(private val id: String) : ZLinkActor {
    override fun actorId(): String = id
    override fun context(): ZLinkActorContext = object : ZLinkActorContext {
        override fun spotRid() = java.util.Optional.empty<RoutingId>()
        override fun isJoined() = false
        override fun boundSession() = null
        override fun getSpot(): ZLinkSpot? = null
        override fun <TSpot : ZLinkSpot?> getSpot(spotType: Class<TSpot>): TSpot? = null
    }
}

data class JoinRequest(val value: String)

data class JoinReply(val value: String)

data class PlayerCommand(val value: String)

data class PlayerReply(val value: String)

data class PlayerEvent(val value: String)
