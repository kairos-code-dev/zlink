package systems.zlink.framework.kotlin

import java.io.IOException
import java.net.ServerSocket
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption
import java.time.Duration
import java.util.Optional
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.future.await
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.service.registry.ServiceRole
import systems.zlink.contracts.service.registry.TopologyState
import systems.zlink.contracts.sockets.SendFlags
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkPublishHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorEvent
import systems.zlink.framework.errors.ZLinkFrameworkException
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle
import systems.zlink.framework.runtime.registry.ZLinkRegistryLifecycle
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient

final class KotlinFrameworkE2ETest {
    @Test
    fun `CH-001 requestToChannel uses Kotlin coroutine awaitReply extension`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addClientServerChannel("profile")
                    .enableServer(endpoint)
                    .serverRoutingId(RoutingId.from("kotlin-profile-server"))
                    .addRequestHandler(EchoHandler::class.java, String::class.java, String::class.java, "Echo")
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addClientServerChannel("profile").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            val reply = client
                .requestToChannel("profile", "kotlin-e2e")
                .packetName("Echo")
                .awaitReply<String>()

            assertEquals("kotlin:kotlin-e2e", reply)
        } finally {
            client.stop()
            server.stop()
        }
    }

    @Test
    fun `CH-006 sendToChannel records server evidence`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        RecordingSendHandler.commands.clear()
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addClientServerChannel("profile")
                    .enableServer(endpoint)
                    .addSendHandler(RecordingSendHandler::class.java, String::class.java, "RecordCommand")
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addClientServerChannel("profile").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            client
                .sendToChannel("profile", "kotlin-send")
                .packetName("RecordCommand")
                .submit()
                .await()

            waitUntil { RecordingSendHandler.commands.contains("kotlin-send") }
            assertEquals(listOf("kotlin-send"), RecordingSendHandler.commands.toList())
        } finally {
            client.stop()
            server.stop()
            RecordingSendHandler.commands.clear()
        }
    }

    @Test
    fun `PUB-001 fanout delivers the same sequence to three subscribers`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        FanoutSequenceOneHandler.messages.clear()
        FanoutSequenceTwoHandler.messages.clear()
        FanoutSequenceThreeHandler.messages.clear()

        val publisher = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addFanoutChannel("sequence").enablePublisher(endpoint)
            },
            backendFactory,
        )
        val first = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addFanoutChannel("sequence")
                    .enableSubscriber(endpoint)
                    .addPublishHandler(FanoutSequenceOneHandler::class.java, String::class.java, "FanoutSequence")
            },
            backendFactory,
        )
        val second = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addFanoutChannel("sequence")
                    .enableSubscriber(endpoint)
                    .addPublishHandler(FanoutSequenceTwoHandler::class.java, String::class.java, "FanoutSequence")
            },
            backendFactory,
        )
        val third = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addFanoutChannel("sequence")
                    .enableSubscriber(endpoint)
                    .addPublishHandler(FanoutSequenceThreeHandler::class.java, String::class.java, "FanoutSequence")
            },
            backendFactory,
        )

        try {
            publisher.start()
            first.start()
            second.start()
            third.start()

            val commonSequence = publishUntilCommonFanoutSequence(publisher)
            assertTrue(FanoutSequenceOneHandler.messages.contains(commonSequence))
            assertTrue(FanoutSequenceTwoHandler.messages.contains(commonSequence))
            assertTrue(FanoutSequenceThreeHandler.messages.contains(commonSequence))
        } finally {
            third.stop()
            second.stop()
            first.stop()
            publisher.stop()
            FanoutSequenceOneHandler.messages.clear()
            FanoutSequenceTwoHandler.messages.clear()
            FanoutSequenceThreeHandler.messages.clear()
        }
    }

    @Test
    fun `CDC-001 JSON codec round-trips nested arrays and nullable fields`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        val request = JsonCodecProbe(
            name = "root",
            revision = 42,
            optionalLabel = null,
            tags = listOf("alpha", "beta"),
            children = listOf(
                JsonCodecChild("first", 1),
                JsonCodecChild("second", 2),
            ),
            nested = JsonCodecChild("nested", 3),
        )
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.addClientServerChannel("codec")
                    .enableServer(endpoint)
                    .addRequestHandler(
                        JsonCodecEchoHandler::class.java,
                        JsonCodecProbe::class.java,
                        JsonCodecProbe::class.java,
                        "JsonCodecProbe",
                    )
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.addClientServerChannel("codec").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            val reply = client
                .requestToChannel("codec", request)
                .packetName("JsonCodecProbe")
                .awaitReply<JsonCodecProbe>()

            assertEquals(request, reply)
            assertEquals(null, reply.optionalLabel)
            assertEquals(listOf("alpha", "beta"), reply.tags)
            assertEquals(JsonCodecChild("nested", 3), reply.nested)
        } finally {
            client.stop()
            server.stop()
        }
    }

    @Test
    fun `DERR-001 missing request handler reports observer and awaitReply fails`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        val errorLatch = CountDownLatch(1)
        val observedError = AtomicReference<ZLinkMessageDispatchErrorEvent>()
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.configureDispatch().setMessageDispatchErrorObserver { error ->
                    observedError.set(error)
                    errorLatch.countDown()
                    CompletableFuture.completedFuture(null)
                }
                options.addClientServerChannel("profile")
                    .enableServer(endpoint)
                    .addRequestHandler(EchoHandler::class.java, String::class.java, String::class.java, "Echo")
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.addClientServerChannel("profile").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            val before = client
                .requestToChannel("profile", "before")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:before", before)

            val failure = try {
                client
                    .requestToChannel("profile", "missing")
                    .packetName("MissingReq")
                    .awaitReply<String>()
                error("missing request handler did not fail")
            } catch (error: ZLinkFrameworkException) {
                error
            }
            assertTrue(failure.message!!.contains("HANDLER_MISSING for packet 'MissingReq'"))
            assertTrue(errorLatch.await(1, TimeUnit.SECONDS), "dispatch error observer was not called")
            val error = observedError.get()
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface())
            assertEquals(ZLinkDispatchMessageKind.REQUEST, error.messageKind())
            assertEquals(ZLinkDispatchErrorReason.HANDLER_MISSING, error.reason())
            assertEquals(ZLinkDispatchErrorAction.REPLY_ERROR, error.action())
            assertEquals("MissingReq", error.packetName())
            assertEquals("profile", error.channelName())

            val after = client
                .requestToChannel("profile", "after")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:after", after)
        } finally {
            client.stop()
            server.stop()
        }
    }

    @Test
    fun `DERR-002 missing send handler reports observer and request path stays alive`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        val errorLatch = CountDownLatch(1)
        val observedError = AtomicReference<ZLinkMessageDispatchErrorEvent>()
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.configureDispatch().setMessageDispatchErrorObserver { error ->
                    observedError.set(error)
                    errorLatch.countDown()
                    CompletableFuture.completedFuture(null)
                }
                options.addClientServerChannel("profile")
                    .enableServer(endpoint)
                    .addRequestHandler(EchoHandler::class.java, String::class.java, String::class.java, "Echo")
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addClientServerChannel("profile").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            val before = client
                .requestToChannel("profile", "before-send-error")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:before-send-error", before)

            client
                .sendToChannel("profile", "missing-send")
                .packetName("MissingCommand")
                .submit()
                .await()

            assertTrue(errorLatch.await(1, TimeUnit.SECONDS), "dispatch error observer was not called")
            val error = observedError.get()
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface())
            assertEquals(ZLinkDispatchMessageKind.SEND, error.messageKind())
            assertEquals(ZLinkDispatchErrorReason.HANDLER_MISSING, error.reason())
            assertEquals(ZLinkDispatchErrorAction.DROP, error.action())
            assertEquals("MissingCommand", error.packetName())
            assertEquals("profile", error.channelName())

            val after = client
                .requestToChannel("profile", "after-send-error")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:after-send-error", after)
        } finally {
            client.stop()
            server.stop()
        }
    }

    @Test
    fun `DERR-006 payload decode failure reports observer and awaitReply can recover`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        val errorLatch = CountDownLatch(1)
        val observedError = AtomicReference<ZLinkMessageDispatchErrorEvent>()
        DecodeProbeHandler.invocations.set(0)
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.configureDispatch().setMessageDispatchErrorObserver { error ->
                    observedError.set(error)
                    errorLatch.countDown()
                    CompletableFuture.completedFuture(null)
                }
                val channel = options.addClientServerChannel("profile")
                    .enableServer(endpoint)
                channel.addRequestHandler(EchoHandler::class.java, String::class.java, String::class.java, "Echo")
                channel.addRequestHandler(
                    DecodeProbeHandler::class.java,
                    DecodePayload::class.java,
                    String::class.java,
                    "DecodeReq",
                )
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.addClientServerChannel("profile").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            val before = client
                .requestToChannel("profile", "before-decode-error")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:before-decode-error", before)

            val channelAdapter = backendFactory.createChannelAdapter(ZLinkBackendAdapterOptions(Duration.ofSeconds(1)))
            channelAdapter.createContext().use { rawContext ->
                channelAdapter.createDealerSocket(rawContext).use { rawDealer ->
                    rawDealer.connect(endpoint)
                    val replyFuture = CompletableFuture<ZLinkBackendReceived>()
                    val malformedParts = listOf(
                        Message.from("DecodeReq"),
                        Message.from("{"),
                    )
                    try {
                        assertTrue(
                            rawDealer.request(
                                malformedParts,
                                { reply -> replyFuture.complete(reply) },
                                SendFlags.NONE,
                                Duration.ofSeconds(2),
                            ),
                        )
                        replyFuture.get(2, TimeUnit.SECONDS).use { reply ->
                            assertEquals("ZLinkFrameworkError", reply.parts()[0].toUtf8String())
                            assertTrue(reply.parts()[1].toUtf8String().contains("PayloadDecodeFailed"))
                        }
                    } finally {
                        malformedParts.forEach(Message::close)
                    }
                }
            }

            assertEquals(0, DecodeProbeHandler.invocations.get())
            assertTrue(errorLatch.await(1, TimeUnit.SECONDS), "dispatch error observer was not called")
            val error = observedError.get()
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface())
            assertEquals(ZLinkDispatchMessageKind.REQUEST, error.messageKind())
            assertEquals(ZLinkDispatchErrorReason.PAYLOAD_DECODE_FAILED, error.reason())
            assertEquals(ZLinkDispatchErrorAction.REPLY_ERROR, error.action())
            assertEquals("DecodeReq", error.packetName())
            assertEquals("profile", error.channelName())
            assertTrue(error.exception().message!!.contains("PayloadDecodeFailed"))

            val after = client
                .requestToChannel("profile", DecodePayload("after-decode-error"))
                .packetName("DecodeReq")
                .awaitReply<String>()
            assertEquals("decode:after-decode-error", after)
            assertEquals(1, DecodeProbeHandler.invocations.get())
        } finally {
            client.stop()
            server.stop()
            DecodeProbeHandler.invocations.set(0)
        }
    }

    @Test
    fun `DERR-007 handler exception reports observer and awaitReply fails`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        val errorLatch = CountDownLatch(1)
        val observedError = AtomicReference<ZLinkMessageDispatchErrorEvent>()
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.configureDispatch().setMessageDispatchErrorObserver { error ->
                    observedError.set(error)
                    errorLatch.countDown()
                    CompletableFuture.completedFuture(null)
                }
                val channel = options.addClientServerChannel("profile")
                    .enableServer(endpoint)
                channel.addRequestHandler(EchoHandler::class.java, String::class.java, String::class.java, "Echo")
                channel.addRequestHandler(ThrowingHandler::class.java, String::class.java, String::class.java, "ThrowReq")
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.addClientServerChannel("profile").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            val before = client
                .requestToChannel("profile", "before-handler-error")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:before-handler-error", before)

            val failure = try {
                client
                    .requestToChannel("profile", "boom")
                    .packetName("ThrowReq")
                    .awaitReply<String>()
                error("handler exception did not fail")
            } catch (error: ZLinkFrameworkException) {
                error
            }
            assertTrue(failure.message!!.contains("DERR-007 handler exception"))
            assertTrue(errorLatch.await(1, TimeUnit.SECONDS), "dispatch error observer was not called")
            val error = observedError.get()
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface())
            assertEquals(ZLinkDispatchMessageKind.REQUEST, error.messageKind())
            assertEquals(ZLinkDispatchErrorReason.HANDLER_EXCEPTION, error.reason())
            assertEquals(ZLinkDispatchErrorAction.REPLY_ERROR, error.action())
            assertEquals("ThrowReq", error.packetName())
            assertEquals("profile", error.channelName())
            assertTrue(error.exception() is IllegalStateException)

            val after = client
                .requestToChannel("profile", "after-handler-error")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:after-handler-error", after)
        } finally {
            client.stop()
            server.stop()
        }
    }

    @Test
    fun `DERR-009 dispatch errors are written to file log`() = runBlocking {
        val endpoint = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        val logPath = Files.createTempFile("zlink-kotlin-derr-009-", ".log")
        val errorLatch = CountDownLatch(3)
        DecodeProbeHandler.invocations.set(0)
        val server = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.configureDispatch().setMessageDispatchErrorObserver { error ->
                    Files.writeString(
                        logPath,
                        dispatchLogLine(error),
                        StandardOpenOption.CREATE,
                        StandardOpenOption.APPEND,
                    )
                    errorLatch.countDown()
                    CompletableFuture.completedFuture(null)
                }
                val channel = options.addClientServerChannel("profile")
                    .enableServer(endpoint)
                channel.addRequestHandler(EchoHandler::class.java, String::class.java, String::class.java, "Echo")
                channel.addRequestHandler(
                    DecodeProbeHandler::class.java,
                    DecodePayload::class.java,
                    String::class.java,
                    "DecodeReq",
                )
                channel.addRequestHandler(ThrowingHandler::class.java, String::class.java, String::class.java, "ThrowReq")
            },
            backendFactory,
        )
        val client = lifecycle(
            DefaultZLinkFrameworkOptions().also { options ->
                options.codecs().addJson()
                options.addClientServerChannel("profile").enableClient(endpoint)
            },
            backendFactory,
        )

        try {
            server.start()
            client.start()

            val before = client
                .requestToChannel("profile", "before-file-log")
                .packetName("Echo")
                .awaitReply<String>()
            assertEquals("kotlin:before-file-log", before)

            try {
                client
                    .requestToChannel("profile", "missing")
                    .packetName("MissingReq")
                    .awaitReply<String>()
                error("missing request handler did not fail")
            } catch (error: ZLinkFrameworkException) {
                assertTrue(error.message!!.contains("MissingReq"))
            }

            val channelAdapter = backendFactory.createChannelAdapter(ZLinkBackendAdapterOptions(Duration.ofSeconds(1)))
            channelAdapter.createContext().use { rawContext ->
                channelAdapter.createDealerSocket(rawContext).use { rawDealer ->
                    rawDealer.connect(endpoint)
                    val replyFuture = CompletableFuture<ZLinkBackendReceived>()
                    val malformedParts = listOf(
                        Message.from("DecodeReq"),
                        Message.from("{"),
                    )
                    try {
                        assertTrue(
                            rawDealer.request(
                                malformedParts,
                                { reply -> replyFuture.complete(reply) },
                                SendFlags.NONE,
                                Duration.ofSeconds(2),
                            ),
                        )
                        replyFuture.get(2, TimeUnit.SECONDS).use { reply ->
                            assertEquals("ZLinkFrameworkError", reply.parts()[0].toUtf8String())
                            assertTrue(reply.parts()[1].toUtf8String().contains("PayloadDecodeFailed"))
                        }
                    } finally {
                        malformedParts.forEach(Message::close)
                    }
                }
            }

            try {
                client
                    .requestToChannel("profile", "boom")
                    .packetName("ThrowReq")
                    .awaitReply<String>()
                error("handler exception did not fail")
            } catch (error: ZLinkFrameworkException) {
                assertTrue(error.message!!.contains("DERR-007 handler exception"))
            }

            assertTrue(errorLatch.await(2, TimeUnit.SECONDS), "dispatch error file log was not written")
            val text = waitForFileLog(logPath, "dispatch-error", 3)
            assertTrue(text.contains("surface=CHANNEL"))
            assertTrue(text.contains("messageKind=REQUEST"))
            assertTrue(text.contains("reason=HANDLER_MISSING"))
            assertTrue(text.contains("reason=PAYLOAD_DECODE_FAILED"))
            assertTrue(text.contains("reason=HANDLER_EXCEPTION"))
            assertTrue(text.contains("action=REPLY_ERROR"))
            assertTrue(text.contains("packetName=MissingReq"))
            assertTrue(text.contains("packetName=DecodeReq"))
            assertTrue(text.contains("packetName=ThrowReq"))
            assertTrue(text.contains("channelName=profile"))
            assertTrue(text.contains("correlationId="))
        } finally {
            client.stop()
            server.stop()
            DecodeProbeHandler.invocations.set(0)
            Files.deleteIfExists(logPath)
        }
    }

    @Test
    fun `DSC-008 requestToChannel traffic survives provider scale out and scale in`() = runBlocking {
        val registryPub = tcpEndpoint()
        val registryRouter = tcpEndpoint()
        val endpointA = tcpEndpoint()
        val endpointB = tcpEndpoint()
        val endpointC = tcpEndpoint()
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        clearObservedRequests()

        val registry = registryLifecycle(registryPub, registryRouter, backendFactory)
        var providerA: ZLinkFrameworkLifecycle? = null
        try {
            registry.start()
            ZLinkRemoteRegistryQueryClient.connect(registryRouter, backendFactory).use { query ->
                val client = lifecycle(clientOptions(registryRouter), backendFactory)
                val secondClient = lifecycle(clientOptions(registryRouter), backendFactory)
                try {
                    client.start()
                    secondClient.start()
                    providerA = lifecycle(
                        providerOptions(registryRouter, endpointA, "provider-a", ProviderAProbeHandler::class.java),
                        backendFactory,
                    )
                    providerA!!.start()
                    awaitReadyEndpoint(query, endpointA)
                    assertReplyFromOneOf(client, "DSC-008:a", setOf("api-a:DSC-008:a"))

                    val stopTraffic = AtomicBoolean(false)
                    val observedProviders = ConcurrentHashMap.newKeySet<String>()
                    val completedRequestIds = ConcurrentLinkedQueue<String>()
                    val trafficA = launch(Dispatchers.Default) {
                        runTransitionTraffic(client, "DSC-008:client-a", stopTraffic, observedProviders, completedRequestIds)
                    }
                    val trafficB = launch(Dispatchers.Default) {
                        runTransitionTraffic(secondClient, "DSC-008:client-b", stopTraffic, observedProviders, completedRequestIds)
                    }

                    var providerB: ZLinkFrameworkLifecycle? = null
                    var providerC: ZLinkFrameworkLifecycle? = null
                    try {
                        delay(150)
                        assertTrue(
                            observedProviders.all { it == "api-a" },
                            "only provider A should be observed before scale-out",
                        )
                        providerB = lifecycle(
                            providerOptions(registryRouter, endpointB, "provider-b", ProviderBProbeHandler::class.java),
                            backendFactory,
                        )
                        providerB.start()
                        awaitReadyEndpoint(query, endpointB)
                        providerC = lifecycle(
                            providerOptions(registryRouter, endpointC, "provider-c", ProviderCProbeHandler::class.java),
                            backendFactory,
                        )
                        providerC.start()
                        awaitTrafficProviders(observedProviders, setOf("api-b", "api-c"))
                        awaitNoDuplicateRequestEvidence(completedRequestIds.toList())

                        providerA!!.stop()
                        providerA = null
                        awaitEndpointNotReady(query, endpointA)
                        delay(150)
                        stopTraffic.set(true)
                        trafficA.join()
                        trafficB.join()
                        awaitNoDuplicateRequestEvidence(completedRequestIds.toList())
                        val apiARequestsBeforeStableScaleIn = observedRequests("api-a").size
                        assertProvidersObserved(
                            listOf(client, secondClient),
                            "DSC-008:in",
                            setOf("api-b", "api-c"),
                            setOf("api-b", "api-c"),
                        )
                        assertEquals(
                            apiARequestsBeforeStableScaleIn,
                            observedRequests("api-a").size,
                            "api-a received requests after scale-in",
                        )
                    } finally {
                        stopTraffic.set(true)
                        trafficA.cancel()
                        trafficB.cancel()
                        providerC?.stop()
                        providerB?.stop()
                    }

                    awaitEndpointNotReady(query, endpointB)
                    awaitEndpointNotReady(query, endpointC)
                } finally {
                    secondClient.stop()
                    client.stop()
                }
            }
        } finally {
            providerA?.stop()
            registry.stop()
            clearObservedRequests()
        }
    }

    @Test
    fun `DSC-009 same routing id with different endpoint replaces discovered provider`() = runBlocking {
        val registryPub = tcpEndpoint()
        val registryRouter = tcpEndpoint()
        val firstEndpoint = tcpEndpoint()
        val replacementEndpoint = tcpEndpoint()
        val routingId = RoutingId.from("api-a")
        val backendFactory = ZLinkJavaBackendAdapterFactory()
        clearObservedRequests()

        val registry = registryLifecycle(registryPub, registryRouter, backendFactory)
        try {
            registry.start()
            ZLinkRemoteRegistryQueryClient.connect(registryRouter, backendFactory).use { query ->
                val client = lifecycle(clientOptions(registryRouter), backendFactory)
                try {
                    client.start()
                    lifecycle(
                        providerOptions(registryRouter, firstEndpoint, routingId, OldProbeHandler::class.java),
                        backendFactory,
                    ).also { first ->
                        try {
                            first.start()
                            awaitSingleReadyRoutingEndpoint(query, routingId, firstEndpoint)
                            assertReplyFromOneOf(client, "DSC-009:first", setOf("old-api:DSC-009:first"))
                        } finally {
                            first.stop()
                        }
                    }

                    awaitEndpointNotReady(query, firstEndpoint)

                    lifecycle(
                        providerOptions(registryRouter, replacementEndpoint, routingId, NewProbeHandler::class.java),
                        backendFactory,
                    ).also { replacement ->
                        try {
                            replacement.start()
                            awaitSingleReadyRoutingEndpoint(query, routingId, replacementEndpoint)
                            awaitEndpointNotReady(query, firstEndpoint)
                            assertSingleRoutingEndpoint(query, routingId, replacementEndpoint)
                            repeat(20) { attempt ->
                                assertReplyFromOneOf(
                                    client,
                                    "DSC-009:replace:$attempt",
                                    setOf("new-api:DSC-009:replace:$attempt"),
                                )
                            }
                        } finally {
                            replacement.stop()
                        }
                    }
                } finally {
                    client.stop()
                }
            }
        } finally {
            registry.stop()
            clearObservedRequests()
        }
    }

    private fun lifecycle(
        options: DefaultZLinkFrameworkOptions,
        backendFactory: ZLinkJavaBackendAdapterFactory,
    ): ZLinkFrameworkLifecycle =
        ZLinkFrameworkLifecycle(options, backendFactory, explicitHandlerFactory())

    private fun explicitHandlerFactory(): ZLinkHandlerFactory =
        ZLinkHandlerFactory { handlerType ->
            when (handlerType) {
                EchoHandler::class.java -> EchoHandler()
                RecordingSendHandler::class.java -> RecordingSendHandler()
                FanoutSequenceOneHandler::class.java -> FanoutSequenceOneHandler()
                FanoutSequenceTwoHandler::class.java -> FanoutSequenceTwoHandler()
                FanoutSequenceThreeHandler::class.java -> FanoutSequenceThreeHandler()
                DecodeProbeHandler::class.java -> DecodeProbeHandler()
                JsonCodecEchoHandler::class.java -> JsonCodecEchoHandler()
                ThrowingHandler::class.java -> ThrowingHandler()
                ProviderAProbeHandler::class.java -> ProviderAProbeHandler()
                ProviderBProbeHandler::class.java -> ProviderBProbeHandler()
                ProviderCProbeHandler::class.java -> ProviderCProbeHandler()
                OldProbeHandler::class.java -> OldProbeHandler()
                NewProbeHandler::class.java -> NewProbeHandler()
                else -> throw IllegalArgumentException("test handler is not registered: ${handlerType.name}")
            }
        }

    private fun registryLifecycle(
        pubEndpoint: String,
        routerEndpoint: String,
        backendFactory: ZLinkJavaBackendAdapterFactory,
    ): ZLinkRegistryLifecycle =
        ZLinkRegistryLifecycle(
            ZLinkEmbeddedRegistryOptions().also { options ->
                options.setPubEndpoint(pubEndpoint)
                options.setRouterEndpoint(routerEndpoint)
            },
            backendFactory,
        )

    private fun providerOptions(
        registryRouter: String,
        endpoint: String,
        routingId: String,
        handlerType: Class<out ZLinkRequestHandler<String, String>>,
    ): DefaultZLinkFrameworkOptions =
        providerOptions(registryRouter, endpoint, RoutingId.from(routingId), handlerType)

    private fun providerOptions(
        registryRouter: String,
        endpoint: String,
        routingId: RoutingId,
        handlerType: Class<out ZLinkRequestHandler<String, String>>,
    ): DefaultZLinkFrameworkOptions =
        DefaultZLinkFrameworkOptions().also { options ->
            options.useDiscovery().addRegistryEndpoint(registryRouter)
            options.addClientServerChannel("profile")
                .enableServer(endpoint)
                .serverRoutingId(routingId)
                .addRequestHandler(handlerType, String::class.java, String::class.java, "Probe")
        }

    private fun clientOptions(registryRouter: String): DefaultZLinkFrameworkOptions =
        DefaultZLinkFrameworkOptions().also { options ->
            options.setDefaultRequestTimeout(Duration.ofMillis(150))
            options.useDiscovery().addRegistryEndpoint(registryRouter)
            options.addClientServerChannel("profile").enableClient()
        }

    private suspend fun assertReplyFromOneOf(
        client: ZLinkFrameworkLifecycle,
        request: String,
        expectedReplies: Set<String>,
    ) {
        val deadline = System.nanoTime() + Duration.ofSeconds(4).toNanos()
        var lastFailure: Throwable? = null
        while (System.nanoTime() < deadline) {
            try {
                val reply = requestReply(client, request)
                assertTrue(expectedReplies.contains(reply), "unexpected DSC reply: $reply, expected $expectedReplies")
                return
            } catch (error: Exception) {
                lastFailure = error
            }
            delay(10)
        }
        throw AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: " +
                "requestToChannel did not reach an expected provider",
            lastFailure,
        )
    }

    private suspend fun assertProvidersObserved(
        clients: List<ZLinkFrameworkLifecycle>,
        requestPrefix: String,
        requiredProviders: Set<String>,
        allowedProviders: Set<String>,
    ) {
        val observedProviders = mutableSetOf<String>()
        val requestIds = mutableListOf<String>()
        val deadline = System.nanoTime() + Duration.ofSeconds(6).toNanos()
        var requestIndex = 0
        var lastFailure: Throwable? = null
        while (System.nanoTime() < deadline &&
            (!observedProviders.containsAll(requiredProviders) || requestIndex < 12)
        ) {
            val client = clients[requestIndex % clients.size]
            val request = "$requestPrefix:$requestIndex"
            requestIds.add(request)
            try {
                val reply = requestReply(client, request)
                val provider = providerName(reply)
                assertTrue(allowedProviders.isEmpty() || allowedProviders.contains(provider), "unexpected provider: $reply")
                observedProviders.add(provider)
            } catch (error: Exception) {
                lastFailure = error
            }
            requestIndex++
        }
        if (!observedProviders.containsAll(requiredProviders)) {
            throw AssertionError(
                "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: " +
                    "required providers were not observed: $requiredProviders, observed $observedProviders",
                lastFailure,
            )
        }
        awaitNoDuplicateRequestEvidence(requestIds)
    }

    private suspend fun runTransitionTraffic(
        client: ZLinkFrameworkLifecycle,
        clientId: String,
        stopTraffic: AtomicBoolean,
        observedProviders: MutableSet<String>,
        completedRequestIds: ConcurrentLinkedQueue<String>,
    ) {
        var requestIndex = 0
        while (!stopTraffic.get()) {
            val request = "$clientId:${requestIndex++}"
            try {
                val reply = requestReply(client, request)
                completedRequestIds.add(request)
                observedProviders.add(providerName(reply))
            } catch (_: Exception) {
                // Transient timeout is allowed while discovery converges. Completed requests are
                // checked for exactly-once evidence.
            }
        }
    }

    private suspend fun awaitTrafficProviders(
        observedProviders: Set<String>,
        requiredProviders: Set<String>,
    ) {
        val deadline = System.nanoTime() + Duration.ofSeconds(6).toNanos()
        while (System.nanoTime() < deadline) {
            if (observedProviders.containsAll(requiredProviders)) {
                return
            }
            delay(10)
        }
        throw AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: " +
                "transition traffic did not observe providers $requiredProviders, observed $observedProviders",
        )
    }

    private suspend fun requestReply(client: ZLinkFrameworkLifecycle, request: String): String =
        client
            .requestToChannel("profile", request)
            .packetName("Probe")
            .timeout(Duration.ofMillis(150))
            .awaitReply()

    private fun providerName(reply: String): String =
        reply.substringBefore(':')

    private suspend fun publishUntilCommonFanoutSequence(publisher: ZLinkFrameworkLifecycle): String {
        val deadline = System.nanoTime() + Duration.ofSeconds(4).toNanos()
        var sequence = 0
        while (System.nanoTime() < deadline) {
            commonFanoutSequence()?.let { return it }
            publisher
                .publish("sequence", "score", "seq:${sequence++}")
                .packetName("FanoutSequence")
                .submit()
                .await()
            delay(10)
        }
        commonFanoutSequence()?.let { return it }
        throw AssertionError(
            "PUB-001 fanout sequence was not delivered to all subscribers: " +
                "first=${FanoutSequenceOneHandler.messages}, " +
                "second=${FanoutSequenceTwoHandler.messages}, " +
                "third=${FanoutSequenceThreeHandler.messages}",
        )
    }

    private fun commonFanoutSequence(): String? =
        FanoutSequenceOneHandler.messages.firstOrNull { value ->
            FanoutSequenceTwoHandler.messages.contains(value) &&
                FanoutSequenceThreeHandler.messages.contains(value)
        }

    private suspend fun awaitReadyEndpoint(
        query: ZLinkRemoteRegistryQueryClient,
        endpoint: String,
    ) {
        awaitTopology(query, endpoint, true)
    }

    private suspend fun awaitEndpointNotReady(
        query: ZLinkRemoteRegistryQueryClient,
        endpoint: String,
    ) {
        awaitTopology(query, endpoint, false)
    }

    private suspend fun awaitSingleReadyRoutingEndpoint(
        query: ZLinkRemoteRegistryQueryClient,
        routingId: RoutingId,
        endpoint: String,
    ) {
        val deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos()
        var lastFailure: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                val readyEntries = query.topology(
                    ZLinkRegistryTopologyFilter(
                        Optional.empty(),
                        Optional.empty(),
                        Optional.of(ServiceRole.ROUTER),
                        Optional.of("profile"),
                        Optional.of(routingId),
                        Optional.of(TopologyState.READY),
                        Optional.empty(),
                    ),
                ).toCompletableFuture().join()
                if (readyEntries.size == 1 && readyEntries[0].endpoint() == endpoint) {
                    return
                }
            } catch (error: RuntimeException) {
                lastFailure = error
            }
            delay(10)
        }
        throw AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: " +
                "routing id did not resolve to a single ready endpoint: $routingId -> $endpoint",
            lastFailure,
        )
    }

    private suspend fun assertSingleRoutingEndpoint(
        query: ZLinkRemoteRegistryQueryClient,
        routingId: RoutingId,
        endpoint: String,
    ) {
        val deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos()
        var lastFailure: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                val entries = query.topology(
                    ZLinkRegistryTopologyFilter(
                        Optional.empty(),
                        Optional.empty(),
                        Optional.of(ServiceRole.ROUTER),
                        Optional.of("profile"),
                        Optional.of(routingId),
                        Optional.empty(),
                        Optional.empty(),
                    ),
                ).toCompletableFuture().join()
                if (entries.size == 1 && entries[0].endpoint() == endpoint) {
                    return
                }
            } catch (error: RuntimeException) {
                lastFailure = error
            }
            delay(10)
        }
        throw AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: " +
                "routing id topology did not contain exactly one replacement endpoint: $routingId -> $endpoint",
            lastFailure,
        )
    }

    private suspend fun awaitTopology(
        query: ZLinkRemoteRegistryQueryClient,
        endpoint: String,
        ready: Boolean,
    ) {
        val deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos()
        var lastFailure: RuntimeException? = null
        while (System.nanoTime() < deadline) {
            try {
                val isReady = query.topology(
                    ZLinkRegistryTopologyFilter(
                        Optional.empty(),
                        Optional.empty(),
                        Optional.of(ServiceRole.ROUTER),
                        Optional.of("profile"),
                        Optional.empty(),
                        Optional.empty(),
                        Optional.empty(),
                    ),
                ).toCompletableFuture().join()
                    .filter { it.endpoint() == endpoint }
                    .any { isReady(it) }
                if (isReady == ready) {
                    return
                }
            } catch (error: RuntimeException) {
                lastFailure = error
            }
            delay(10)
        }
        throw AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: " +
                "registry topology did not reach expected readiness for $endpoint",
            lastFailure,
        )
    }

    private fun isReady(entry: ZLinkRegistryTopologyEntry): Boolean =
        entry.state() == TopologyState.READY

    private fun assertNoDuplicateRequestEvidence(requestIds: List<String>) {
        val counts = HashMap<String, Int>()
        for (provider in listOf("api-a", "api-b", "api-c", "old-api", "new-api")) {
            for (requestId in observedRequests(provider)) {
                counts[requestId] = counts.getOrDefault(requestId, 0) + 1
            }
        }
        for (requestId in requestIds) {
            assertEquals(1, counts.getOrDefault(requestId, 0), "request evidence must be recorded by exactly one provider: $requestId")
        }
    }

    private suspend fun awaitNoDuplicateRequestEvidence(requestIds: List<String>) {
        var lastFailure: AssertionError? = null
        val deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos()
        while (System.nanoTime() < deadline) {
            try {
                assertNoDuplicateRequestEvidence(requestIds)
                return
            } catch (error: AssertionError) {
                lastFailure = error
            }
            delay(10)
        }
        lastFailure?.let { throw it }
    }

    class EchoHandler : ZLinkRequestHandler<String, String> {
        override fun handle(request: String, context: ZLinkRequestContext): String =
            "kotlin:$request"
    }

    class RecordingSendHandler : ZLinkSendHandler<String> {
        override fun handle(message: String, context: ZLinkSendContext) {
            commands.add(message)
        }

        companion object {
            val commands: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
        }
    }

    class FanoutSequenceOneHandler : ZLinkPublishHandler<String> {
        override fun handle(message: String, context: ZLinkPublishContext) {
            messages.add(message)
        }

        companion object {
            val messages: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
        }
    }

    class FanoutSequenceTwoHandler : ZLinkPublishHandler<String> {
        override fun handle(message: String, context: ZLinkPublishContext) {
            messages.add(message)
        }

        companion object {
            val messages: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
        }
    }

    class FanoutSequenceThreeHandler : ZLinkPublishHandler<String> {
        override fun handle(message: String, context: ZLinkPublishContext) {
            messages.add(message)
        }

        companion object {
            val messages: ConcurrentLinkedQueue<String> = ConcurrentLinkedQueue()
        }
    }

    class ThrowingHandler : ZLinkRequestHandler<String, String> {
        override fun handle(request: String, context: ZLinkRequestContext): String =
            throw IllegalStateException("DERR-007 handler exception")
    }

    data class DecodePayload(val value: String = "")

    data class JsonCodecProbe(
        val name: String = "",
        val revision: Int = 0,
        val optionalLabel: String? = null,
        val tags: List<String> = emptyList(),
        val children: List<JsonCodecChild> = emptyList(),
        val nested: JsonCodecChild = JsonCodecChild(),
    )

    data class JsonCodecChild(
        val name: String = "",
        val rank: Int = 0,
    )

    class DecodeProbeHandler : ZLinkRequestHandler<DecodePayload, String> {
        override fun handle(request: DecodePayload, context: ZLinkRequestContext): String {
            invocations.incrementAndGet()
            return "decode:${request.value}"
        }

        companion object {
            val invocations = AtomicInteger()
        }
    }

    class JsonCodecEchoHandler : ZLinkRequestHandler<JsonCodecProbe, JsonCodecProbe> {
        override fun handle(request: JsonCodecProbe, context: ZLinkRequestContext): JsonCodecProbe =
            request
    }

    class ProviderAProbeHandler : ZLinkRequestHandler<String, String> {
        override fun handle(request: String, context: ZLinkRequestContext): String {
            recordRequest("api-a", request)
            return "api-a:$request"
        }
    }

    class ProviderBProbeHandler : ZLinkRequestHandler<String, String> {
        override fun handle(request: String, context: ZLinkRequestContext): String {
            recordRequest("api-b", request)
            return "api-b:$request"
        }
    }

    class ProviderCProbeHandler : ZLinkRequestHandler<String, String> {
        override fun handle(request: String, context: ZLinkRequestContext): String {
            recordRequest("api-c", request)
            return "api-c:$request"
        }
    }

    class OldProbeHandler : ZLinkRequestHandler<String, String> {
        override fun handle(request: String, context: ZLinkRequestContext): String {
            recordRequest("old-api", request)
            return "old-api:$request"
        }
    }

    class NewProbeHandler : ZLinkRequestHandler<String, String> {
        override fun handle(request: String, context: ZLinkRequestContext): String {
            recordRequest("new-api", request)
            return "new-api:$request"
        }
    }

    companion object {
        private val nextPort = AtomicInteger(48_000 + (ProcessHandle.current().pid() % 4_000).toInt())
        private val observedRequests: ConcurrentHashMap<String, MutableSet<String>> = ConcurrentHashMap()

        private suspend fun waitUntil(predicate: () -> Boolean) {
            repeat(50) {
                if (predicate()) {
                    return
                }
                kotlinx.coroutines.delay(20)
            }
            assertTrue(predicate(), "condition was not met before timeout")
        }

        private fun tcpEndpoint(): String =
            "tcp://127.0.0.1:${allocatePort()}"

        private fun dispatchLogLine(error: ZLinkMessageDispatchErrorEvent): String =
            "dispatch-error" +
                " surface=${error.surface()}" +
                " messageKind=${error.messageKind()}" +
                " reason=${error.reason()}" +
                " action=${error.action()}" +
                " packetName=${error.packetName()}" +
                " channelName=${error.channelName()}" +
                " correlationId=${error.correlationId()}" +
                System.lineSeparator()

        private suspend fun waitForFileLog(path: Path, marker: String, expected: Int): String {
            repeat(80) {
                val text = Files.readString(path)
                if (countOccurrences(text, marker) >= expected) {
                    return text
                }
                delay(25)
            }
            return Files.readString(path)
        }

        private fun countOccurrences(text: String, marker: String): Int {
            var count = 0
            var index = text.indexOf(marker)
            while (index >= 0) {
                count += 1
                index = text.indexOf(marker, index + marker.length)
            }
            return count
        }

        private fun allocatePort(): Int {
            repeat(200) {
                val port = nextPort.getAndIncrement()
                if (isBindable(port)) {
                    return port
                }
            }
            error("failed to allocate tcp port")
        }

        private fun isBindable(port: Int): Boolean =
            try {
                ServerSocket(port).use { server ->
                    server.reuseAddress = false
                    true
                }
            } catch (_: IOException) {
                false
            }

        private fun recordRequest(provider: String, request: String) {
            observedRequests.computeIfAbsent(provider) { ConcurrentHashMap.newKeySet() }.add(request)
        }

        private fun observedRequests(provider: String): Set<String> =
            observedRequests[provider] ?: emptySet()

        private fun clearObservedRequests() {
            observedRequests.clear()
        }
    }
}
