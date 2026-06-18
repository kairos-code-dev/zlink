package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import io.netty.buffer.UnpooledByteBufAllocator;
import io.netty.handler.ssl.SslContextBuilder;
import io.netty.handler.ssl.SslHandler;
import io.netty.handler.ssl.util.InsecureTrustManagerFactory;
import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class ZLinkStreamConnectorTest {
    private final List<ZLinkStreamConnector> connectors = new ArrayList<>();

    @AfterEach
    void closeConnectors() throws Exception {
        for (ZLinkStreamConnector connector : connectors) {
            connector.close().await();
        }
        connectors.clear();
    }

    private ZLinkStreamConnector createConnector(ZLinkStreamConnectorOptions options) {
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(options);
        connectors.add(connector);
        return connector;
    }

    @Test
    void defaultNameResolverUsesPacketNameAnnotationValue() {
        assertEquals(
            "custom.packet",
            ZLinkStreamPacketNameResolver.defaultResolver().resolve(NamedPayload.class));
    }

    @Test
    void defaultNameResolverPreservesBlankPacketNameAnnotationValue() {
        assertEquals(
            "",
            ZLinkStreamPacketNameResolver.defaultResolver().resolve(BlankNamedPayload.class));
    }

    @Test
    void manualDispatchInvokesRegisteredHandlerOnlyWhenDispatched() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            AtomicInteger handled = new AtomicInteger();
            connector.on("Ping", message -> {
                handled.incrementAndGet();
                assertEquals("Ping", message.packetName());
                assertEquals("42", message.metadata().get("seq"));
                message.payload().payload().close();
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    ZLinkStreamWireProtocol.FLAG_HAS_METADATA,
                    null,
                    "Ping",
                    Map.of("seq", "42")),
                TcpStreamConnectorTestServer.bytes("hello")).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> connector.pendingDispatchCount() == 1);
            assertEquals(1, connector.pendingDispatchCount());
            assertEquals(1, connector.receivedCount("Ping"));
            assertEquals(0, handled.get());

            connector.dispatch().await();

            assertEquals(0, connector.pendingDispatchCount());
            assertEquals(1, connector.receivedCount("Ping"));
            assertEquals(1, handled.get());
        }
    }

    @Test
    void requestWritesFrameAndCorrelatesResponse() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "hello"))
                .packetName("EchoOverride")
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            assertEquals(ZLinkStreamWireProtocol.KIND_REQUEST, request.header().kind());
            assertEquals("EchoOverride", request.header().name());
            assertEquals("hello", new String(request.payload(), StandardCharsets.UTF_8));

            server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                    request,
                    "EchoOverride",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("reply")).join();

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals("EchoOverride", reply.packetName());
                assertEquals("reply", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
            }
        }
    }

    @Test
    void inboundObserverSeesResponseBeforePendingRequestCompletesWithoutWaitingForCallback()
        throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            CountDownLatch observed = new CountDownLatch(1);
            CountDownLatch releaseObserver = new CountDownLatch(1);
            AtomicReference<ZLinkStreamInboundObservation> snapshot = new AtomicReference<>();
            connector.observeInbound(observation -> {
                snapshot.set(observation);
                observed.countDown();
                try {
                    releaseObserver.await(5, TimeUnit.SECONDS);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                }
                return CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "hello"))
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                    request,
                    "EchoReply",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("reply")).join();

            ZLinkStreamEncodedPayload reply = replyFuture.get(1, TimeUnit.SECONDS);
            try {
                assertEquals("reply", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
                releaseObserver.countDown();
            }

            assertTrue(observed.await(1, TimeUnit.SECONDS));
            assertEquals(ZLinkStreamMessageKind.RESPONSE, snapshot.get().kind());
            assertEquals("EchoReply", snapshot.get().packetName());
            assertEquals(request.header().requestSeq(), snapshot.get().requestSeq());
        }
    }

    @Test
    void inboundObserverSeesSendAndControlFrames() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            List<ZLinkStreamInboundObservation> observations = new ArrayList<>();
            connector.observeInbound(observation -> {
                synchronized (observations) {
                    observations.add(observation);
                }
                return CompletableFuture.completedFuture(null);
            });
            AtomicInteger handled = new AtomicInteger();
            connector.on("Notice", message -> {
                handled.incrementAndGet();
                message.payload().payload().close();
                return CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    0,
                    null,
                    "Notice",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("body")).join();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_CONTROL,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    0,
                    null,
                    "$zlink.heartbeat.pong",
                    Map.of()),
                new byte[0]).join();

            TcpStreamConnectorTestServer.awaitCondition(() -> {
                synchronized (observations) {
                    return observations.stream()
                        .anyMatch(item -> item.kind() == ZLinkStreamMessageKind.CONTROL);
                }
            });

            assertEquals(1, handled.get());
            synchronized (observations) {
                assertTrue(observations.stream()
                    .anyMatch(item -> item.kind() == ZLinkStreamMessageKind.SEND
                        && item.packetName().equals("Notice")));
                assertTrue(observations.stream()
                    .anyMatch(item -> item.kind() == ZLinkStreamMessageKind.CONTROL));
            }
        }
    }

    @Test
    void inboundObserverRejectsAfterConnectAndStopsAfterClose() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            AtomicInteger observed = new AtomicInteger();
            AutoCloseable registration = connector.observeInbound(observation -> {
                observed.incrementAndGet();
                return CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            assertThrows(IllegalStateException.class,
                () -> connector.observeInbound(observation ->
                    CompletableFuture.completedFuture(null)));
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    0,
                    null,
                    "First",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("a")).join();
            TcpStreamConnectorTestServer.awaitCondition(() -> observed.get() == 1);

            registration.close();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    0,
                    null,
                    "Second",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("b")).join();
            Thread.sleep(50);

            assertEquals(1, observed.get());
        }
    }

    @Test
    void inboundObserverFailureReportsObserverFailedAndDispatchContinues() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            AtomicReference<ZLinkStreamError> error = new AtomicReference<>();
            connector.onErrorReceived(received -> {
                if (received.code() == ZLinkStreamErrorCode.OBSERVER_FAILED) {
                    error.set(received);
                }
                return CompletableFuture.completedFuture(null);
            });
            connector.observeInbound(observation -> {
                throw new IllegalStateException("observer failed");
            });
            AtomicInteger handled = new AtomicInteger();
            connector.on("Notice", message -> {
                handled.incrementAndGet();
                message.payload().payload().close();
                return CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    0,
                    null,
                    "Notice",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("body")).join();

            TcpStreamConnectorTestServer.awaitCondition(() -> error.get() != null);
            TcpStreamConnectorTestServer.awaitCondition(() -> handled.get() == 1);
            assertEquals(ZLinkStreamErrorCode.OBSERVER_FAILED, error.get().code());
            assertEquals(1, handled.get());
        }
    }

    @Test
    void inboundObserverOverflowReportsObserverDroppedAndRequestsStillComplete()
        throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            CountDownLatch observerEntered = new CountDownLatch(1);
            CountDownLatch releaseObserver = new CountDownLatch(1);
            List<String> observedNames = Collections.synchronizedList(new ArrayList<>());
            connector.observeInbound(observation -> {
                observedNames.add(observation.packetName());
                observerEntered.countDown();
                try {
                    releaseObserver.await(5, TimeUnit.SECONDS);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                }
                return CompletableFuture.completedFuture(null);
            });
            AtomicReference<ZLinkStreamError> dropped = new AtomicReference<>();
            connector.onErrorReceived(error -> {
                if (error.code() == ZLinkStreamErrorCode.OBSERVER_DROPPED) {
                    dropped.set(error);
                }
                return CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    0,
                    null,
                    "BlockObserver",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("block")).join();
            assertTrue(observerEntered.await(1, TimeUnit.SECONDS));

            for (int index = 0;
                index < ZLinkStreamInboundObserverDispatcher.DEFAULT_MAX_NOTIFICATIONS + 2;
                index++) {
                var requestFrame = server.readFrameAsync();
                var replyFuture = connector.request(payload("Echo", "hello"))
                    .timeout(Duration.ofMillis(500))
                    .submit()
                    .toCompletableFuture();
                TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
                server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                        request,
                        "EchoReply" + index,
                        Map.of()),
                    TcpStreamConnectorTestServer.bytes("reply")).join();
                ZLinkStreamEncodedPayload reply = replyFuture.get(1, TimeUnit.SECONDS);
                reply.payload().close();
            }

            TcpStreamConnectorTestServer.awaitCondition(() -> dropped.get() != null);
            assertEquals(ZLinkStreamErrorCode.OBSERVER_DROPPED, dropped.get().code());
            releaseObserver.countDown();
            TcpStreamConnectorTestServer.awaitCondition(
                () -> observedNames.contains("EchoReply0"));
            assertFalse(observedNames.contains(
                "EchoReply" + (ZLinkStreamInboundObserverDispatcher.DEFAULT_MAX_NOTIFICATIONS + 1)));
        }
    }

    @Test
    void sendBulkMetadataReplacesExistingMetadata() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var frame = server.readFrameAsync();
            connector.send(payload("Meta", "hello"))
                .metadata("old", "ignored")
                .metadata(Map.of("trace", "abc", "tenant", "sample"))
                .submit()
                .toCompletableFuture()
                .join();

            TcpStreamConnectorTestServer.ReceivedFrame sent = frame.join();
            assertEquals(ZLinkStreamWireProtocol.KIND_SEND, sent.header().kind());
            assertEquals("Meta", sent.header().name());
            assertEquals(Map.of("trace", "abc", "tenant", "sample"), sent.header().metadata());
        }
    }

    @Test
    void encodedPayloadCodecIsWrittenToWireHeaderLikeDotnet() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var frame = server.readFrameAsync();
            connector.send(new ZLinkStreamEncodedPayload(
                    "JsonPayload",
                    Message.from("{\"ok\":true}"),
                    Map.of(),
                    ZLinkStreamCodec.JSON))
                .submit()
                .toCompletableFuture()
                .join();

            TcpStreamConnectorTestServer.ReceivedFrame sent = frame.join();
            assertEquals(ZLinkStreamWireProtocol.KIND_SEND, sent.header().kind());
            assertEquals(ZLinkStreamWireProtocol.CODEC_JSON, sent.header().codec());
            assertEquals("JsonPayload", sent.header().name());
        }
    }

    @Test
    void requestBulkMetadataReplacesExistingMetadata() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("MetaRequest", "hello"))
                .metadata("old", "ignored")
                .metadata(Map.of("trace", "abc", "tenant", "sample"))
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            assertEquals(ZLinkStreamWireProtocol.KIND_REQUEST, request.header().kind());
            assertEquals("MetaRequest", request.header().name());
            assertEquals(Map.of("trace", "abc", "tenant", "sample"), request.header().metadata());

            server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                    request,
                    "MetaRequest",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("reply")).join();

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals("MetaRequest", reply.packetName());
            } finally {
                reply.payload().close();
            }
        }
    }

    @Test
    void lz4PicklerMatchesDotnetFixtures() {
        assertArrayEquals(
            hex("00636F6D70726573736564"),
            ZLinkStreamLz4Pickler.pickle(TcpStreamConnectorTestServer.bytes("compressed")));
        assertArrayEquals(
            TcpStreamConnectorTestServer.bytes("compressed"),
            ZLinkStreamLz4Pickler.unpickle(hex("00636F6D70726573736564")));
        assertArrayEquals(
            "A".repeat(1024).getBytes(StandardCharsets.UTF_8),
            ZLinkStreamLz4Pickler.unpickle(hex("80F2031F410100FFFFFFEA504141414141")));
    }

    @Test
    void sendCompressionIsExplicit() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(compressedOptions(server.endpoint(), ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var plainFrame = server.readFrameAsync();
            connector.send(payload("Plain", "A".repeat(1024)))
                .submit()
                .toCompletableFuture()
                .join();

            TcpStreamConnectorTestServer.ReceivedFrame plain = plainFrame.join();
            assertEquals(0, plain.header().flags() & ZLinkStreamWireProtocol.FLAG_PAYLOAD_COMPRESSED);
            assertEquals("A".repeat(1024), new String(plain.payload(), StandardCharsets.UTF_8));

            var compressedFrame = server.readFrameAsync();
            connector.send(payload("Compressed", "A".repeat(1024)))
                .compress()
                .submit()
                .toCompletableFuture()
                .join();

            TcpStreamConnectorTestServer.ReceivedFrame compressed = compressedFrame.join();
            assertTrue((compressed.header().flags() & ZLinkStreamWireProtocol.FLAG_PAYLOAD_COMPRESSED) != 0);
            assertEquals(
                "A".repeat(1024),
                new String(ZLinkStreamLz4Pickler.unpickle(compressed.payload()), StandardCharsets.UTF_8));
        }
    }

    @Test
    void compressedSendUsesOriginalPayloadForMaxSizeValidation() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(compressedOptions(server.endpoint(), ZLinkStreamDispatchMode.MANUAL, 8));
            connector.connect().await();

            assertThrows(IllegalArgumentException.class, () ->
                connector.send(payload("Compressed", "A".repeat(1024)))
                    .compress()
                    .submit());
        }
    }

    @Test
    void compressedResponseIsDecompressedBeforeCompletingRequest() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(compressedOptions(server.endpoint(), ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "hello"))
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_RESPONSE,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    ZLinkStreamWireProtocol.FLAG_HAS_REQUEST_SEQ
                        | ZLinkStreamWireProtocol.FLAG_PAYLOAD_COMPRESSED,
                    request.header().requestSeq(),
                    "Echo",
                    Map.of()),
                ZLinkStreamLz4Pickler.pickle(TcpStreamConnectorTestServer.bytes("compressed-reply"))).join();

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals(
                    "compressed-reply",
                    new String(reply.payload().toByteArray(), StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
            }
        }
    }

    @Test
    void compressedResponseRejectsDecodedPayloadAboveReceiveLimit() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(options(
                    server.endpoint(),
                    ZLinkStreamDispatchMode.MANUAL,
                    64 * 1024,
                    2,
                    false,
                    false,
                    ZLinkStreamCompression.LZ4));
            connector.connect().await();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "hello"))
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_RESPONSE,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    ZLinkStreamWireProtocol.FLAG_HAS_REQUEST_SEQ
                        | ZLinkStreamWireProtocol.FLAG_PAYLOAD_COMPRESSED,
                    request.header().requestSeq(),
                    "Echo",
                    Map.of()),
                new byte[] {0x40, 0x03}).join();

            CompletionException ex = assertThrows(CompletionException.class, replyFuture::join);
            assertTrue(ex.getCause() instanceof IllegalArgumentException);
        }
    }

    @Test
    void lz4PicklerRejectsDecodedPayloadAboveReceiveLimit() {
        assertThrows(
            IllegalArgumentException.class,
            () -> ZLinkStreamLz4Pickler.unpickle(new byte[] {0x40, 0x03}, 2));
    }

    @Test
    void webSocketRequestUsesBinaryFrameAndCorrelatesResponse() throws Exception {
        try (WebSocketStreamConnectorTestServer server = new WebSocketStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "hello"))
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            assertEquals(ZLinkStreamWireProtocol.KIND_REQUEST, request.header().kind());
            assertEquals("Echo", request.header().name());
            assertEquals("hello", new String(request.payload(), StandardCharsets.UTF_8));

            server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                    request,
                    "Echo",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("reply")).join();

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals("Echo", reply.packetName());
                assertEquals("reply", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
            }
        }
    }

    @Test
    void tcpTransportRejectsOversizedInboundPayloadPrefix() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(options(
                    server.endpoint(),
                    ZLinkStreamDispatchMode.MANUAL,
                    64 * 1024,
                    1,
                    false,
                    false,
                    ZLinkStreamCompression.NONE));
            connector.connect().await();

            server.sendBytesAsync(framePrefix(0, 2)).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> connector.state() == ZLinkStreamConnectionState.DISCONNECTED);
        }
    }

    @Test
    void tlsRequestUsesEncryptedFrameAndSkippedCertificateValidation() throws Exception {
        try (TlsStreamConnectorTestServer server = new TlsStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "hello"))
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            assertEquals(ZLinkStreamWireProtocol.KIND_REQUEST, request.header().kind());
            assertEquals("Echo", request.header().name());
            assertEquals("hello", new String(request.payload(), StandardCharsets.UTF_8));

            server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                    request,
                    "Echo",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("reply")).join();

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals("Echo", reply.packetName());
                assertEquals("reply", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
            }
        }
    }

    @Test
    void tlsTransportRejectsOversizedInboundPayloadPrefix() throws Exception {
        try (TlsStreamConnectorTestServer server = new TlsStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(options(
                    server.endpoint(),
                    ZLinkStreamDispatchMode.MANUAL,
                    64 * 1024,
                    1,
                    false,
                    true,
                    ZLinkStreamCompression.NONE));
            connector.connect().await();

            server.sendBytesAsync(framePrefix(0, 2)).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> connector.state() == ZLinkStreamConnectionState.DISCONNECTED);
        }
    }

    @Test
    void tlsHandlerEnablesHttpsEndpointIdentificationUnlessValidationIsSkipped() throws Exception {
        var sslContext = SslContextBuilder.forClient()
            .trustManager(InsecureTrustManagerFactory.INSTANCE)
            .build();

        SslHandler verified = ZLinkTlsTransportConnection.createSslHandler(
            sslContext,
            UnpooledByteBufAllocator.DEFAULT,
            "localhost",
            443,
            false);
        try {
            assertEquals(
                "HTTPS",
                verified.engine()
                    .getSSLParameters()
                    .getEndpointIdentificationAlgorithm());
        } finally {
            verified.engine().closeOutbound();
        }

        SslHandler skipped = ZLinkTlsTransportConnection.createSslHandler(
            sslContext,
            UnpooledByteBufAllocator.DEFAULT,
            "localhost",
            443,
            true);
        try {
            assertNull(skipped.engine()
                .getSSLParameters()
                .getEndpointIdentificationAlgorithm());
        } finally {
            skipped.engine().closeOutbound();
        }
    }

    @Test
    void wssRequestUsesBinaryFrameAndSkippedCertificateValidation() throws Exception {
        try (SecureWebSocketStreamConnectorTestServer server =
                 new SecureWebSocketStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "hello"))
                .timeout(Duration.ofMillis(500))
                .submit()
                .toCompletableFuture();

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            assertEquals(ZLinkStreamWireProtocol.KIND_REQUEST, request.header().kind());
            assertEquals("Echo", request.header().name());
            assertEquals("hello", new String(request.payload(), StandardCharsets.UTF_8));

            server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                    request,
                    "Echo",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("reply")).join();

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals("Echo", reply.packetName());
                assertEquals("reply", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
            }
        }
    }

    @Test
    void webSocketTransportRejectsOversizedInboundPayloadPrefix() throws Exception {
        try (WebSocketStreamConnectorTestServer server = new WebSocketStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(options(
                    server.endpoint(),
                    ZLinkStreamDispatchMode.MANUAL,
                    64 * 1024,
                    1,
                    false,
                    false,
                    ZLinkStreamCompression.NONE));
            connector.connect().await();

            server.sendRawAsync(framePrefix(0, 2)).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> connector.state() == ZLinkStreamConnectionState.DISCONNECTED);
        }
    }

    @Test
    void requestWithoutReplyFailsWithTimeoutCause() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            connector.connect().await();

            CompletionException ex = assertThrows(CompletionException.class, () ->
                connector.request(payload("MissingReply", "hello"))
                    .timeout(Duration.ofMillis(10))
                    .submit()
                    .toCompletableFuture()
                    .join());

            assertTrue(ex.getCause() instanceof java.util.concurrent.TimeoutException);
            assertEquals(0, connector.pendingDispatchCount());
        }
    }

    @Test
    void connectAndCloseUpdateState() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            assertFalse(connector.isConnected());
            assertEquals(ZLinkStreamDispatchMode.AUTO, connector.options().dispatchMode());
            connector.connect().await();
            assertTrue(connector.isConnected());
            connector.close().await();
            assertEquals(ZLinkStreamConnectionState.CLOSED, connector.state());
        }
    }

    @Test
    void reconnectRestoresConnectionAfterTransportDisconnect() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            List<ZLinkStreamConnectionState> states = new ArrayList<>();
            connector.onConnectionStateChanged(state -> {
                states.add(state);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            connector.disconnect().await();
            assertFalse(connector.isConnected());

            connector.reconnect().await();

            assertTrue(connector.isConnected());
            assertEquals(List.of(
                ZLinkStreamConnectionState.CONNECTED,
                ZLinkStreamConnectionState.DISCONNECTED,
                ZLinkStreamConnectionState.RECONNECTING,
                ZLinkStreamConnectionState.CONNECTED), states);
        }
    }

    @Test
    void lifecycleHandlersObserveStateChangesAndDisconnect() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            List<ZLinkStreamConnectionState> states = new ArrayList<>();
            AtomicInteger disconnected = new AtomicInteger();

            AutoCloseable stateRegistration = connector.onConnectionStateChanged(state -> {
                states.add(state);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });
            AutoCloseable disconnectedRegistration = connector.onDisconnected(() -> {
                disconnected.incrementAndGet();
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            connector.close().await();

            assertEquals(List.of(
                ZLinkStreamConnectionState.CONNECTED,
                ZLinkStreamConnectionState.CLOSED), states);
            assertEquals(1, disconnected.get());

            stateRegistration.close();
            disconnectedRegistration.close();
            connector.close().await();

            assertEquals(2, states.size());
            assertEquals(1, disconnected.get());
        }
    }

    @Test
    void errorReceivedObservesInvalidHeaderAfterManualDispatch() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.MANUAL));
            AtomicReference<ZLinkStreamError> received = new AtomicReference<>();
            connector.onErrorReceived(error -> {
                received.set(error);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            server.sendRawAsync(
                "invalid-header".getBytes(StandardCharsets.UTF_8),
                TcpStreamConnectorTestServer.bytes("payload")).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> connector.pendingDispatchCount() == 1);
            assertEquals(null, received.get());

            connector.dispatch().await();

            assertEquals(ZLinkStreamErrorCode.FRAME_DECODE_FAILED, received.get().code());
        }
    }

    @Test
    void errorReceivedObservesRemoteErrorPacket() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            AtomicReference<ZLinkStreamError> received = new AtomicReference<>();
            connector.onErrorReceived(error -> {
                received.set(error);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connect().await();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_ERROR,
                    ZLinkStreamWireProtocol.CODEC_JSON,
                    0,
                    null,
                    "RemoteError",
                    Map.of()),
                "{\"message\":\"remote failed\"}".getBytes(StandardCharsets.UTF_8)).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> received.get() != null);

            assertEquals(ZLinkStreamErrorCode.REMOTE_ERROR, received.get().code());
            assertEquals("{\"message\":\"remote failed\"}", received.get().message());
        }
    }

    @Test
    void errorReceivedObservesUserCallbackFailure() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                createConnector(server.options(ZLinkStreamDispatchMode.AUTO));
            AtomicReference<ZLinkStreamError> received = new AtomicReference<>();
            connector.onErrorReceived(error -> {
                received.set(error);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });
            connector.on("Ping", message -> {
                throw new IllegalStateException("boom");
            });

            connector.connect().await();
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    0,
                    null,
                    "Ping",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("hello")).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> received.get() != null);

            assertEquals(ZLinkStreamErrorCode.USER_CALLBACK_FAILED, received.get().code());
        }
    }

    @Test
    void reservedPacketNamesAreRejectedForUserHandlers() throws Exception {
        ZLinkStreamConnector connector =
            createConnector(options(ZLinkStreamDispatchMode.MANUAL));
        try {
            assertThrows(IllegalArgumentException.class,
                () -> connector.on("$zlink.heartbeat", message ->
                    java.util.concurrent.CompletableFuture.completedFuture(null)));
            assertThrows(IllegalArgumentException.class,
                () -> connector.send(payload("$zlink.send", "hello")));
            assertThrows(IllegalArgumentException.class,
                () -> connector.request(payload("$zlink.request", "hello")));
            assertThrows(IllegalArgumentException.class,
                () -> connector.send(payload("Ping", "hello")).packetName("$zlink.override"));
            assertThrows(IllegalArgumentException.class,
                () -> connector.request(payload("Ping", "hello")).packetName("$zlink.override"));
        } finally {
            connector.close().await();
        }
    }

    @Test
    void packetNameLongerThanOneByteLengthIsRejected() throws Exception {
        ZLinkStreamConnector connector =
            createConnector(options(ZLinkStreamDispatchMode.MANUAL));
        try {
            String tooLong = "a".repeat(256);

            assertThrows(IllegalArgumentException.class,
                () -> connector.on(tooLong, message ->
                    java.util.concurrent.CompletableFuture.completedFuture(null)));
            assertThrows(IllegalArgumentException.class,
                () -> connector.send(payload(tooLong, "hello")));
            assertThrows(IllegalArgumentException.class,
                () -> connector.request(payload(tooLong, "hello")));
        } finally {
            connector.close().await();
        }
    }

    @Test
    void uriSchemeAndTransportMismatchIsRejected() {
        assertThrows(IllegalArgumentException.class, () -> createConnector(
            new ZLinkStreamConnectorOptions(
                URI.create("http://127.0.0.1:7000"),
                ZLinkStreamDispatchMode.MANUAL,
                Duration.ofSeconds(1),
                1)));
        assertThrows(IllegalArgumentException.class, () -> createConnector(
            new ZLinkStreamConnectorOptions(
                URI.create("127.0.0.1:7000"),
                ZLinkStreamDispatchMode.MANUAL,
                Duration.ofSeconds(1),
                1)));
    }

    @Test
    void skipServerCertificateValidationDefaultsToFalseAndCanBeEnabled() {
        assertFalse(new ZLinkStreamConnectorOptions(
            URI.create("wss://127.0.0.1:7000"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(1),
            1).skipServerCertificateValidation());

        assertTrue(new ZLinkStreamConnectorOptions(
            URI.create("wss://127.0.0.1:7000"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(1),
            1,
            Duration.ofSeconds(1),
            64 * 1024,
            false,
            Duration.ofMillis(25),
            Duration.ofMillis(500),
            true,
            Duration.ofMillis(10),
            Duration.ofMillis(250),
            2.0,
            true).skipServerCertificateValidation());
    }

    @Test
    void defaultOptionsMatchDotnetConnectorDefaults() {
        URI endpoint = URI.create("tcp://127.0.0.1:7000");
        ZLinkStreamConnectorOptions options =
            ZLinkStreamConnectorOptions.createDefault(endpoint);

        assertEquals(endpoint, options.endpoint());
        assertEquals(ZLinkStreamDispatchMode.MANUAL, options.dispatchMode());
        assertEquals(Duration.ofSeconds(30), options.requestTimeout());
        assertEquals(3, options.maxReconnectAttempts());
        assertEquals(Duration.ofSeconds(5), options.connectTimeout());
        assertEquals(64 * 1024, options.maxSendPayloadSize());
        assertEquals(64 * 1024, options.maxReceivePayloadSize());
        assertTrue(options.heartbeatEnabled());
        assertEquals(Duration.ofSeconds(1), options.heartbeatInterval());
        assertEquals(Duration.ofSeconds(5), options.heartbeatTimeout());
        assertTrue(options.reconnectEnabled());
        assertEquals(Duration.ofMillis(250), options.reconnectInitialDelay());
        assertEquals(Duration.ofSeconds(5), options.reconnectMaxDelay());
        assertEquals(2.0, options.reconnectBackoffFactor());
        assertFalse(options.skipServerCertificateValidation());
        assertEquals(ZLinkStreamCompression.NONE, options.compression());
        assertEquals("custom.packet", options.nameResolver().resolve(NamedPayload.class));
    }

    @Test
    void receivePayloadLimitMustBePositive() {
        assertThrows(IllegalArgumentException.class, () ->
            createConnector(options(
                URI.create("tcp://127.0.0.1:1"),
                ZLinkStreamDispatchMode.MANUAL,
                64 * 1024,
                0,
                false,
                false,
                ZLinkStreamCompression.NONE)));
    }

    private static ZLinkStreamEncodedPayload payload(String packetName, String body) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(body),
            Map.of());
    }

    private static ZLinkStreamConnectorOptions options(
        ZLinkStreamDispatchMode dispatchMode) {
        return new ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:1"),
            dispatchMode,
            Duration.ofSeconds(1),
            1);
    }

    private static ZLinkStreamConnectorOptions compressedOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode) {
        return compressedOptions(endpoint, dispatchMode, 64 * 1024);
    }

    private static ZLinkStreamConnectorOptions compressedOptions(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        int maxSendPayloadSize) {
        return options(
            endpoint,
            dispatchMode,
            maxSendPayloadSize,
            64 * 1024,
            true,
            false,
            ZLinkStreamCompression.LZ4);
    }

    private static ZLinkStreamConnectorOptions options(
        URI endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        int maxSendPayloadSize,
        int maxReceivePayloadSize,
        boolean reconnectEnabled,
        boolean skipServerCertificateValidation,
        ZLinkStreamCompression compression) {
        return new ZLinkStreamConnectorOptions(
            endpoint,
            dispatchMode,
            Duration.ofSeconds(1),
            1,
            Duration.ofSeconds(1),
            maxSendPayloadSize,
            maxReceivePayloadSize,
            false,
            Duration.ofMillis(25),
            Duration.ofMillis(500),
            reconnectEnabled,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            skipServerCertificateValidation,
            compression,
            ZLinkStreamPacketNameResolver.defaultResolver(),
            null);
    }

    private static byte[] framePrefix(int headerLength, int payloadLength) {
        return ByteBuffer.allocate(6)
            .putShort((short) headerLength)
            .putInt(payloadLength)
            .array();
    }

    private static byte[] hex(String value) {
        byte[] result = new byte[value.length() / 2];
        for (int i = 0; i < result.length; i++) {
            result[i] = (byte) Integer.parseInt(value.substring(i * 2, i * 2 + 2), 16);
        }
        return result;
    }

    @ZLinkStreamPacketName("custom.packet")
    private record NamedPayload(String value) {
    }

    @ZLinkStreamPacketName("")
    private record BlankNamedPayload(String value) {
    }
}
