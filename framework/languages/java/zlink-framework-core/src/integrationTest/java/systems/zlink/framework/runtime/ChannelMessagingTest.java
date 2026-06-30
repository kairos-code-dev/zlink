package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.logging.Handler;
import java.util.logging.LogRecord;
import java.util.logging.Logger;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRouteSendContext;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSend;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

final class ChannelMessagingTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(32_000 + (int) (ProcessHandle.current().pid() % 10_000));
    private static final AtomicReference<CountDownLatch> FANOUT_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> FANOUT_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> FANOUT_TOPIC = new AtomicReference<>();
    private static final AtomicReference<String> FANOUT_CHANNEL = new AtomicReference<>();
    private static final CopyOnWriteArrayList<String> FANOUT_SEQUENCE_ONE = new CopyOnWriteArrayList<>();
    private static final CopyOnWriteArrayList<String> FANOUT_SEQUENCE_TWO = new CopyOnWriteArrayList<>();
    private static final CopyOnWriteArrayList<String> FANOUT_SEQUENCE_THREE = new CopyOnWriteArrayList<>();
    private static final AtomicReference<CountDownLatch> MANUAL_REG_PUBLISH_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> MANUAL_REG_PUBLISH_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> MANUAL_REG_PUBLISH_TOPIC = new AtomicReference<>();
    private static final AtomicReference<String> MANUAL_REG_PUBLISH_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<CountDownLatch> SEND_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> SEND_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> SEND_PACKET = new AtomicReference<>();
    private static final AtomicReference<String> SEND_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<CountDownLatch> MANUAL_REG_SEND_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> MANUAL_REG_SEND_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> MANUAL_REG_SEND_PACKET = new AtomicReference<>();
    private static final AtomicReference<String> MANUAL_REG_SEND_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<CountDownLatch> ROUTE_SEND_LATCH = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_MESSAGE = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_PACKET = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_SEND_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<RoutingId> ROUTE_SEND_SOURCE = new AtomicReference<>();
    private static final AtomicReference<String> ROUTE_REQUEST_CHANNEL = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_REQUEST = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_PACKET = new AtomicReference<>();
    private static final AtomicReference<String> FILTER_CHANNEL = new AtomicReference<>();
    private static final RoutingId SPOT_EGRESS_TARGET_NODE_RID =
        RoutingId.from("spot-egress-target-spot");
    private static final RoutingId SPOT_EGRESS_TARGET_ROUTE_RID =
        RoutingId.from("spot-egress-target-route");

    @Test
    @DisplayName("CH-001 manual client-server request-response")
    void manualClientServer_requestReplySucceeds() {
        String endpoint = "inproc://zlink-java-profile-" + UUID.randomUUID();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("hello", reply);
        }
    }

    @Test
    void handlerFiltersWrapChannelRequestDispatch() {
        String endpoint = "inproc://zlink-java-filtered-profile-" + UUID.randomUUID();
        FILTER_REQUEST.set(null);
        FILTER_PACKET.set(null);
        FILTER_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useFilter(ReplyDecoratingFilter.class);
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("filtered:hello", reply);
            assertEquals("hello", FILTER_REQUEST.get());
            assertEquals("Echo", FILTER_PACKET.get());
            assertEquals("profile", FILTER_CHANNEL.get());
        } finally {
            FILTER_REQUEST.set(null);
            FILTER_PACKET.set(null);
            FILTER_CHANNEL.set(null);
        }
    }

    @Test
    @DisplayName("CH-006 manual client-server send one-way records server evidence")
    void manualClientServer_sendDispatchesToHandler() throws InterruptedException {
        String endpoint = "inproc://zlink-java-notify-" + UUID.randomUUID();
        CountDownLatch latch = new CountDownLatch(1);
        SEND_LATCH.set(latch);
        SEND_MESSAGE.set(null);
        SEND_PACKET.set(null);
        SEND_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addSendHandler(ProfileChangedHandler.class, String.class, "ProfileChanged"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            sendUntilDelivered(runtime);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "client/server send was not delivered");
            assertEquals("changed", SEND_MESSAGE.get());
            assertEquals("ProfileChanged", SEND_PACKET.get());
            assertEquals("profile", SEND_CHANNEL.get());
        } finally {
            SEND_LATCH.set(null);
            SEND_MESSAGE.set(null);
            SEND_PACKET.set(null);
            SEND_CHANNEL.set(null);
        }
    }

    @Test
    @DisplayName("DERR-001 manual client-server missing request handler replies error and reports observer")
    void manualClientServer_missingRequestHandlerRepliesErrorAndReportsObserver()
        throws InterruptedException {
        String endpoint = "inproc://zlink-java-dispatch-error-" + UUID.randomUUID();
        CountDownLatch errorLatch = new CountDownLatch(1);
        AtomicReference<ZLinkMessageFlowEvent> observedError = new AtomicReference<>();
        CopyOnWriteArrayList<String> logMessages = new CopyOnWriteArrayList<>();
        Logger logger = Logger.getLogger(ZLinkMessageFlowTracer.class.getName());
        Handler logHandler = new Handler() {
            @Override
            public void publish(LogRecord record) {
                logMessages.add(record.getMessage());
            }

            @Override
            public void flush() {
            }

            @Override
            public void close() {
            }
        };
        logger.addHandler(logHandler);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch();
            dispatch.setMessageFlowObserver(error -> {
                observedError.set(error);
                errorLatch.countDown();
                return CompletableFuture.completedFuture(null);
            }); };
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String before = runtime.client()
                .requestToChannel("profile", message("before"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("before", before);
            String markerText = runtime.client()
                .requestToChannel("profile", message("ZLinkFrameworkError:not-error"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("ZLinkFrameworkError:not-error", markerText);
            String markerOnlyText = runtime.client()
                .requestToChannel("profile", message("ZLinkFrameworkError"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("ZLinkFrameworkError", markerOnlyText);

            CompletionException failure = assertThrows(CompletionException.class, () -> runtime.client()
                .requestToChannel("profile", message("missing"))
                .packetName("MissingReq")
                .submit(String.class)
                .toCompletableFuture()
                .join());
            assertTrue(failure.getCause() instanceof ZLinkFrameworkException);
            assertTrue(failure.getCause().getMessage().contains(
                "HANDLER_MISSING for packet 'MissingReq'"));

            assertTrue(errorLatch.await(1, TimeUnit.SECONDS),
                "dispatch error observer was not called");
            ZLinkMessageFlowEvent error = observedError.get();
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface());
            assertEquals(ZLinkDispatchMessageKind.REQUEST, error.messageKind());
            assertEquals(ZLinkDispatchErrorReason.HANDLER_MISSING, error.errorReason());
            assertEquals(ZLinkDispatchErrorAction.REPLY_ERROR, error.errorAction());
            assertEquals("MissingReq", error.packetName());
            assertEquals("profile", error.channelName());
            assertTrue(logMessages.stream().anyMatch(message ->
                message.contains("reason=HANDLER_MISSING")
                    && message.contains("action=REPLY_ERROR")
                    && message.contains("packet=MissingReq")
                    && message.contains("channel=profile")),
                "dispatch error log marker was not written");

            String after = runtime.client()
                .requestToChannel("profile", message("after"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("after", after);
        } finally {
            logger.removeHandler(logHandler);
        }
    }

    @Test
    @DisplayName("DERR-002 manual client-server missing send handler reports observer and keeps request path alive")
    void manualClientServer_missingSendHandlerReportsObserverAndKeepsRequestPathAlive()
        throws InterruptedException {
        String endpoint = "inproc://zlink-java-dispatch-send-error-" + UUID.randomUUID();
        CountDownLatch errorLatch = new CountDownLatch(1);
        AtomicReference<ZLinkMessageFlowEvent> observedError = new AtomicReference<>();
        CopyOnWriteArrayList<String> logMessages = new CopyOnWriteArrayList<>();
        Logger logger = Logger.getLogger(ZLinkMessageFlowTracer.class.getName());
        Handler logHandler = new Handler() {
            @Override
            public void publish(LogRecord record) {
                logMessages.add(record.getMessage());
            }

            @Override
            public void flush() {
            }

            @Override
            public void close() {
            }
        };
        logger.addHandler(logHandler);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch();
            dispatch.setMessageFlowObserver(error -> {
                observedError.set(error);
                errorLatch.countDown();
                return CompletableFuture.completedFuture(null);
            }); };
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String before = runtime.client()
                .requestToChannel("profile", message("before-send-error"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("before-send-error", before);

            runtime.client()
                .sendToChannel("profile", message("missing-send"))
                .packetName("MissingCommand")
                .submit()
                .toCompletableFuture()
                .join();

            assertTrue(errorLatch.await(1, TimeUnit.SECONDS),
                "dispatch error observer was not called");
            ZLinkMessageFlowEvent error = observedError.get();
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface());
            assertEquals(ZLinkDispatchMessageKind.SEND, error.messageKind());
            assertEquals(ZLinkDispatchErrorReason.HANDLER_MISSING, error.errorReason());
            assertEquals(ZLinkDispatchErrorAction.DROP, error.errorAction());
            assertEquals("MissingCommand", error.packetName());
            assertEquals("profile", error.channelName());
            assertTrue(logMessages.stream().anyMatch(message ->
                message.contains("reason=HANDLER_MISSING")
                    && message.contains("action=DROP")
                    && message.contains("packet=MissingCommand")
                    && message.contains("channel=profile")),
                "dispatch error log marker was not written");

            String after = runtime.client()
                .requestToChannel("profile", message("after-send-error"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("after-send-error", after);
        } finally {
            logger.removeHandler(logHandler);
        }
    }

    @Test
    @DisplayName("DERR-006 manual client-server payload decode failure replies error and reports observer")
    void manualClientServer_payloadDecodeFailureRepliesErrorAndReportsObserver()
        throws Exception {
        String endpoint = tcpEndpoint();
        CountDownLatch errorLatch = new CountDownLatch(1);
        AtomicReference<ZLinkMessageFlowEvent> observedError = new AtomicReference<>();
        CopyOnWriteArrayList<String> logMessages = new CopyOnWriteArrayList<>();
        Logger logger = Logger.getLogger(ZLinkMessageFlowTracer.class.getName());
        Handler logHandler = new Handler() {
            @Override
            public void publish(LogRecord record) {
                logMessages.add(record.getMessage());
            }

            @Override
            public void flush() {
            }

            @Override
            public void close() {
            }
        };
        logger.addHandler(logHandler);
        DecodeProbeHandler.invocations.set(0);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch();
            dispatch.setMessageFlowObserver(error -> {
                observedError.set(error);
                errorLatch.countDown();
                return CompletableFuture.completedFuture(null);
            }); };
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(DecodeProbeHandler.class, DecodePayload.class, String.class, "DecodeReq"); };

        ZLinkJavaBackendAdapterFactory backendFactory = new ZLinkJavaBackendAdapterFactory();
        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.startFramework(options, backendFactory)) {
            String before = runtime.client()
                .requestToChannel("profile", message("before-decode-error"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("before-decode-error", before);

            var channelAdapter = backendFactory.createChannelAdapter(
                new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
            try (var rawContext = channelAdapter.createContext();
                 var rawDealer = channelAdapter.createDealerSocket(rawContext)) {
                rawDealer.connect(endpoint);
                CompletableFuture<ZLinkBackendReceived> replyFuture = new CompletableFuture<>();
                List<Message> malformedParts = List.of(
                    Message.from("DecodeReq"),
                    Message.from("{"));
                try {
                    assertTrue(rawDealer.request(
                        malformedParts,
                        replyFuture::complete,
                        SendFlags.NONE,
                        Duration.ofSeconds(2)));
                    try (ZLinkBackendReceived reply = replyFuture.get(2, TimeUnit.SECONDS)) {
                        assertEquals("ZLinkFrameworkError", reply.parts().get(0).toUtf8String());
                        assertTrue(reply.parts().get(1).toUtf8String().contains("PayloadDecodeFailed"));
                    }
                } finally {
                    malformedParts.forEach(Message::close);
                }
            }

            assertEquals(0, DecodeProbeHandler.invocations.get());
            assertTrue(errorLatch.await(1, TimeUnit.SECONDS),
                "dispatch error observer was not called");
            ZLinkMessageFlowEvent error = observedError.get();
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface());
            assertEquals(ZLinkDispatchMessageKind.REQUEST, error.messageKind());
            assertEquals(ZLinkDispatchErrorReason.PAYLOAD_DECODE_FAILED, error.errorReason());
            assertEquals(ZLinkDispatchErrorAction.REPLY_ERROR, error.errorAction());
            assertEquals("DecodeReq", error.packetName());
            assertEquals("profile", error.channelName());
            assertEquals("PayloadDecodeDispatchException", error.errorType());
            assertTrue(error.errorMessage().contains("PayloadDecodeFailed"));
            assertTrue(logMessages.stream().anyMatch(message ->
                message.contains("reason=PAYLOAD_DECODE_FAILED")
                    && message.contains("action=REPLY_ERROR")
                    && message.contains("packet=DecodeReq")
                    && message.contains("channel=profile")),
                "dispatch error log marker was not written");

            String after = runtime.client()
                .requestToChannel("profile", new DecodePayload("after-decode-error"))
                .packetName("DecodeReq")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("decode:after-decode-error", after);
            assertEquals(1, DecodeProbeHandler.invocations.get());
        } finally {
            logger.removeHandler(logHandler);
            DecodeProbeHandler.invocations.set(0);
        }
    }

    @Test
    @DisplayName("DERR-007 manual client-server handler exception replies error and reports observer")
    void manualClientServer_handlerExceptionRepliesErrorAndReportsObserver()
        throws InterruptedException {
        String endpoint = "inproc://zlink-java-dispatch-handler-error-" + UUID.randomUUID();
        CountDownLatch errorLatch = new CountDownLatch(1);
        AtomicReference<ZLinkMessageFlowEvent> observedError = new AtomicReference<>();
        CopyOnWriteArrayList<String> logMessages = new CopyOnWriteArrayList<>();
        Logger logger = Logger.getLogger(ZLinkMessageFlowTracer.class.getName());
        Handler logHandler = new Handler() {
            @Override
            public void publish(LogRecord record) {
                logMessages.add(record.getMessage());
            }

            @Override
            public void flush() {
            }

            @Override
            public void close() {
            }
        };
        logger.addHandler(logHandler);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch();
            dispatch.setMessageFlowObserver(error -> {
                observedError.set(error);
                errorLatch.countDown();
                return CompletableFuture.completedFuture(null);
            }); };
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(ThrowingRequestHandler.class, String.class, String.class, "ThrowReq"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String before = runtime.client()
                .requestToChannel("profile", message("before-handler-error"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("before-handler-error", before);

            CompletionException failure = assertThrows(CompletionException.class, () -> runtime.client()
                .requestToChannel("profile", message("boom"))
                .packetName("ThrowReq")
                .submit(String.class)
                .toCompletableFuture()
                .join());
            assertTrue(failure.getCause() instanceof ZLinkFrameworkException);
            assertTrue(failure.getCause().getMessage().contains("DERR-007 handler exception"));

            assertTrue(errorLatch.await(1, TimeUnit.SECONDS),
                "dispatch error observer was not called");
            ZLinkMessageFlowEvent error = observedError.get();
            assertEquals(ZLinkDispatchErrorSurface.CHANNEL, error.surface());
            assertEquals(ZLinkDispatchMessageKind.REQUEST, error.messageKind());
            assertEquals(ZLinkDispatchErrorReason.HANDLER_EXCEPTION, error.errorReason());
            assertEquals(ZLinkDispatchErrorAction.REPLY_ERROR, error.errorAction());
            assertEquals("ThrowReq", error.packetName());
            assertEquals("profile", error.channelName());
            assertEquals("IllegalStateException", error.errorType());
            assertEquals("DERR-007 handler exception", error.errorMessage());
            assertTrue(logMessages.stream().anyMatch(message ->
                message.contains("reason=HANDLER_EXCEPTION")
                    && message.contains("action=REPLY_ERROR")
                    && message.contains("packet=ThrowReq")
                    && message.contains("channel=profile")),
                "dispatch error log marker was not written");

            String after = runtime.client()
                .requestToChannel("profile", message("after-handler-error"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("after-handler-error", after);
        } finally {
            logger.removeHandler(logHandler);
        }
    }

    @Test
    @DisplayName("DERR-009 manual client-server dispatch errors are written to file log")
    void manualClientServer_dispatchErrorsAreWrittenToFileLog() throws Exception {
        String endpoint = tcpEndpoint();
        Path logPath = Files.createTempFile("zlink-java-derr-009-", ".log");
        CountDownLatch errors = new CountDownLatch(3);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch();
            dispatch.setMessageFlowObserver(error -> {
                try {
                    Files.writeString(
                        logPath,
                        dispatchLogLine(error),
                        StandardCharsets.UTF_8,
                        StandardOpenOption.CREATE,
                        StandardOpenOption.APPEND);
                } catch (IOException ex) {
                    return CompletableFuture.failedFuture(ex);
                }
                errors.countDown();
                return CompletableFuture.completedFuture(null);
            }); };
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(DecodeProbeHandler.class, DecodePayload.class, String.class, "DecodeReq");
            channel.addRequestHandler(ThrowingRequestHandler.class, String.class, String.class, "ThrowReq"); };

        ZLinkJavaBackendAdapterFactory backendFactory = new ZLinkJavaBackendAdapterFactory();
        DecodeProbeHandler.invocations.set(0);
        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.startFramework(options, backendFactory)) {
            String before = runtime.client()
                .requestToChannel("profile", message("before-file-log"))
                .packetName("Echo")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("before-file-log", before);

            CompletionException missing = assertThrows(CompletionException.class, () -> runtime.client()
                .requestToChannel("profile", message("missing"))
                .packetName("MissingReq")
                .submit(String.class)
                .toCompletableFuture()
                .join());
            assertTrue(missing.getCause() instanceof ZLinkFrameworkException);

            var channelAdapter = backendFactory.createChannelAdapter(
                new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
            try (var rawContext = channelAdapter.createContext();
                 var rawDealer = channelAdapter.createDealerSocket(rawContext)) {
                rawDealer.connect(endpoint);
                CompletableFuture<ZLinkBackendReceived> replyFuture = new CompletableFuture<>();
                List<Message> malformedParts = List.of(
                    Message.from("DecodeReq"),
                    Message.from("{"));
                try {
                    assertTrue(rawDealer.request(
                        malformedParts,
                        replyFuture::complete,
                        SendFlags.NONE,
                        Duration.ofSeconds(2)));
                    try (ZLinkBackendReceived reply = replyFuture.get(2, TimeUnit.SECONDS)) {
                        assertEquals("ZLinkFrameworkError", reply.parts().get(0).toUtf8String());
                        assertTrue(reply.parts().get(1).toUtf8String().contains("PayloadDecodeFailed"));
                    }
                } finally {
                    malformedParts.forEach(Message::close);
                }
            }

            CompletionException thrown = assertThrows(CompletionException.class, () -> runtime.client()
                .requestToChannel("profile", message("boom"))
                .packetName("ThrowReq")
                .submit(String.class)
                .toCompletableFuture()
                .join());
            assertTrue(thrown.getCause() instanceof ZLinkFrameworkException);

            assertTrue(errors.await(2, TimeUnit.SECONDS), "dispatch error file log was not written");
            String logText = waitForFileLog(logPath, "dispatch-error", 3);
            assertTrue(logText.contains("surface=CHANNEL"));
            assertTrue(logText.contains("messageKind=REQUEST"));
            assertTrue(logText.contains("reason=HANDLER_MISSING"));
            assertTrue(logText.contains("reason=PAYLOAD_DECODE_FAILED"));
            assertTrue(logText.contains("reason=HANDLER_EXCEPTION"));
            assertTrue(logText.contains("action=REPLY_ERROR"));
            assertTrue(logText.contains("packetName=MissingReq"));
            assertTrue(logText.contains("packetName=DecodeReq"));
            assertTrue(logText.contains("packetName=ThrowReq"));
            assertTrue(logText.contains("channelName=profile"));
            assertTrue(logText.contains("correlationId="));
        } finally {
            DecodeProbeHandler.invocations.set(0);
            Files.deleteIfExists(logPath);
        }
    }

    @Test
    @DisplayName("CDC-001 JSON codec round-trips nested arrays and nullable fields")
    void jsonCodec_roundTripsNestedArraysAndNullableFields() {
        String endpoint = "inproc://zlink-java-codec-" + UUID.randomUUID();
        JsonCodecProbe request = new JsonCodecProbe(
            "root",
            42,
            null,
            List.of("alpha", "beta"),
            List.of(
                new JsonCodecChild("first", 1),
                new JsonCodecChild("second", 2)),
            new JsonCodecChild("nested", 3));

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("codec").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addRequestHandler(
                JsonCodecEchoHandler.class,
                JsonCodecProbe.class,
                JsonCodecProbe.class,
                "JsonCodecProbe"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            JsonCodecProbe reply = runtime.client()
                .requestToChannel("codec", request)
                .packetName("JsonCodecProbe")
                .submit(JsonCodecProbe.class)
                .toCompletableFuture()
                .join();

            assertEquals(request, reply);
            assertNull(reply.optionalLabel());
            assertEquals(List.of("alpha", "beta"), reply.tags());
            assertEquals(new JsonCodecChild("nested", 3), reply.nested());
        }
    }

    @Test
    @DisplayName("REG-003 manual channel handlers dispatch registered packets and report missing packets")
    void manualChannelHandlers_dispatchRegisteredPacketsAndReportMissingPackets()
        throws Exception {
        String endpoint = tcpEndpoint();
        String fanoutEndpoint = tcpEndpoint();
        CountDownLatch sendLatch = new CountDownLatch(1);
        CountDownLatch publishLatch = new CountDownLatch(1);
        CountDownLatch errorLatch = new CountDownLatch(3);
        CopyOnWriteArrayList<ZLinkMessageFlowEvent> observedErrors = new CopyOnWriteArrayList<>();
        MANUAL_REG_SEND_LATCH.set(sendLatch);
        MANUAL_REG_SEND_MESSAGE.set(null);
        MANUAL_REG_SEND_PACKET.set(null);
        MANUAL_REG_SEND_CHANNEL.set(null);
        MANUAL_REG_PUBLISH_LATCH.set(publishLatch);
        MANUAL_REG_PUBLISH_MESSAGE.set(null);
        MANUAL_REG_PUBLISH_TOPIC.set(null);
        MANUAL_REG_PUBLISH_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions serverOptions = new DefaultZLinkFrameworkOptions();
        { var dispatch = serverOptions.configureDispatch();
            dispatch.setMessageFlowObserver(error -> {
                observedErrors.add(error);
                errorLatch.countDown();
                return CompletableFuture.completedFuture(null);
            }); };
        { var channel = serverOptions.addClientServerChannel("manual-reg").enableServer(endpoint);
            channel.addRequestHandler(
                ManualRegistrationRequestHandler.class,
                String.class,
                String.class,
                "ManualRegisteredReq");
            channel.addSendHandler(
                ManualRegistrationCommandHandler.class,
                String.class,
                "ManualRegisteredCommand"); };

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        { var channel = clientOptions.addClientServerChannel("manual-reg");
            channel.enableClient(endpoint); };

        DefaultZLinkFrameworkOptions publisherOptions = new DefaultZLinkFrameworkOptions();
        { var channel = publisherOptions.addFanoutChannel("manual-events").enablePublisher(fanoutEndpoint); };

        DefaultZLinkFrameworkOptions subscriberOptions = new DefaultZLinkFrameworkOptions();
        { var dispatch = subscriberOptions.configureDispatch();
            dispatch.setMessageFlowObserver(error -> {
                observedErrors.add(error);
                errorLatch.countDown();
                return CompletableFuture.completedFuture(null);
            }); };
        { var channel = subscriberOptions.addFanoutChannel("manual-events");
            channel.enableSubscriber(fanoutEndpoint);
            channel.addPublishHandler(
                ManualRegistrationPublishHandler.class,
                String.class,
                "ManualRegisteredEvent"); };

        try (ZLinkFrameworkRuntime ignoredServer =
                 RuntimeTestSupport.startFramework(serverOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime publisher =
                 RuntimeTestSupport.startFramework(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredSubscriber =
                 RuntimeTestSupport.startFramework(subscriberOptions, new ZLinkJavaBackendAdapterFactory())) {
            String reply = client.client()
                .requestToChannel("manual-reg", message("registered"))
                .packetName("ManualRegisteredReq")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            assertEquals("manual:registered", reply);

            client.client()
                .sendToChannel("manual-reg", message("command"))
                .packetName("ManualRegisteredCommand")
                .submit()
                .toCompletableFuture()
                .join();
            assertTrue(sendLatch.await(1, TimeUnit.SECONDS), "manual send was not delivered");
            assertEquals("command", MANUAL_REG_SEND_MESSAGE.get());
            assertEquals("ManualRegisteredCommand", MANUAL_REG_SEND_PACKET.get());
            assertEquals("manual-reg", MANUAL_REG_SEND_CHANNEL.get());

            publishManualRegistrationUntilDelivered(publisher, "ManualRegisteredEvent", "published");
            assertTrue(publishLatch.await(1, TimeUnit.SECONDS), "manual publish was not delivered");
            assertEquals("published", MANUAL_REG_PUBLISH_MESSAGE.get());
            assertEquals("manual", MANUAL_REG_PUBLISH_TOPIC.get());
            assertEquals("manual-events", MANUAL_REG_PUBLISH_CHANNEL.get());

            CompletionException missingRequest = assertThrows(CompletionException.class, () -> client.client()
                .requestToChannel("manual-reg", message("missing"))
                .packetName("ManualMissingReq")
                .submit(String.class)
                .toCompletableFuture()
                .join());
            assertTrue(missingRequest.getCause() instanceof ZLinkFrameworkException);
            assertTrue(missingRequest.getCause().getMessage().contains(
                "HANDLER_MISSING for packet 'ManualMissingReq'"));

            client.client()
                .sendToChannel("manual-reg", message("missing-command"))
                .packetName("ManualMissingCommand")
                .submit()
                .toCompletableFuture()
                .join();

            publishManualRegistrationUntilObserved(
                publisher,
                observedErrors,
                "ManualMissingEvent");

            assertTrue(errorLatch.await(1, TimeUnit.SECONDS), "manual registration errors were not reported");
            assertTrue(waitForManualRegistrationErrors(observedErrors),
                "manual registration request, send, and publish errors were not all reported");
            assertTrue(hasDispatchError(
                observedErrors,
                ZLinkDispatchMessageKind.REQUEST,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                ZLinkDispatchErrorAction.REPLY_ERROR,
                "ManualMissingReq",
                "manual-reg"));
            assertTrue(hasDispatchError(
                observedErrors,
                ZLinkDispatchMessageKind.SEND,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                ZLinkDispatchErrorAction.DROP,
                "ManualMissingCommand",
                "manual-reg"));
            assertTrue(hasDispatchError(
                observedErrors,
                ZLinkDispatchMessageKind.PUBLISH,
                ZLinkDispatchErrorReason.HANDLER_MISSING,
                ZLinkDispatchErrorAction.DROP,
                "ManualMissingEvent",
                "manual-events"));
        } finally {
            MANUAL_REG_SEND_LATCH.set(null);
            MANUAL_REG_SEND_MESSAGE.set(null);
            MANUAL_REG_SEND_PACKET.set(null);
            MANUAL_REG_SEND_CHANNEL.set(null);
            MANUAL_REG_PUBLISH_LATCH.set(null);
            MANUAL_REG_PUBLISH_MESSAGE.set(null);
            MANUAL_REG_PUBLISH_TOPIC.set(null);
            MANUAL_REG_PUBLISH_CHANNEL.set(null);
        }
    }

    @Test
    @DisplayName("REG-001 partial scanned handler group request-reply succeeds")
    void scannedHandlerGroup_requestReplySucceeds() {
        String endpoint = "inproc://zlink-java-scanned-profile-" + UUID.randomUUID();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(ChannelMessagingTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addHandlerGroup("scanned-profile"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("String")
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("scanned:hello", reply);
        }
    }

    @Test
    @DisplayName("REG-002 partial annotation handler group dispatches request and send")
    void scannedMethodHandlerGroup_requestAndSendDispatch() throws InterruptedException {
        String endpoint = "inproc://zlink-java-annotated-profile-" + UUID.randomUUID();
        CountDownLatch latch = new CountDownLatch(1);
        SEND_LATCH.set(latch);
        SEND_MESSAGE.set(null);
        SEND_PACKET.set(null);

        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(ChannelMessagingTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addHandlerGroup("annotated-profile"); };

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
            String reply = runtime.client()
                .requestToChannel("profile", message("hello"))
                .packetName("AnnotatedEcho")
                .submit(String.class)
                .toCompletableFuture()
                .join();
            runtime.client()
                .sendToChannel("profile", message("changed"))
                .packetName("ProfileChanged")
                .submit()
                .toCompletableFuture()
                .join();

            assertEquals("annotated:hello", reply);
            assertTrue(latch.await(1, TimeUnit.SECONDS), "annotated send was not delivered");
            assertEquals("changed", SEND_MESSAGE.get());
            assertEquals("ProfileChanged", SEND_PACKET.get());
        } finally {
            SEND_LATCH.set(null);
            SEND_MESSAGE.set(null);
            SEND_PACKET.set(null);
        }
    }

    @Test
    @DisplayName("PUB-001 partial REG-002 annotation fanout publish dispatches")
    void scannedMethodHandlerGroup_publishDispatches() throws InterruptedException {
        String endpoint = tcpEndpoint();
        CountDownLatch latch = new CountDownLatch(1);
        FANOUT_LATCH.set(latch);
        FANOUT_MESSAGE.set(null);
        FANOUT_TOPIC.set(null);
        FANOUT_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions publisherOptions = new DefaultZLinkFrameworkOptions();
        { var channel = publisherOptions.addFanoutChannel("events").enablePublisher(endpoint); };

        DefaultZLinkFrameworkOptions subscriberOptions = new DefaultZLinkFrameworkOptions();
        subscriberOptions.addHandlersFromPackageOf(ChannelMessagingTest.class);
        { var channel = subscriberOptions.addFanoutChannel("events"); channel.enableSubscriber(endpoint);
            channel.addHandlerGroup("annotated-events"); };

        try (ZLinkFrameworkRuntime ignoredPublisher =
                 RuntimeTestSupport.startFramework(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime subscriber =
                 RuntimeTestSupport.startFramework(subscriberOptions, new ZLinkJavaBackendAdapterFactory())) {
            publishUntilDelivered(ignoredPublisher);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "annotated publish was not delivered");
            assertEquals("home:1", FANOUT_MESSAGE.get());
            assertEquals("score", FANOUT_TOPIC.get());
        } finally {
            FANOUT_LATCH.set(null);
            FANOUT_MESSAGE.set(null);
            FANOUT_TOPIC.set(null);
        }
    }

    @Test
    void discoveryClientServer_requestReplySucceeds() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String serverEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        DefaultZLinkFrameworkOptions serverOptions = new DefaultZLinkFrameworkOptions();
        { var discovery = serverOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = serverOptions.addClientServerChannel("profile").enableServer(serverEndpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        clientOptions.setDefaultRequestTimeout(Duration.ofMillis(100));
        { var discovery = clientOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = clientOptions.addClientServerChannel("profile"); channel.enableClient(); };

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime ignoredServer =
                 RuntimeTestSupport.startFramework(serverOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("hello", awaitDiscoveryReply(client));
        }
    }

    @Test
    void discoveryClientServer_clientStartedBeforeServerFindsLaterProvider() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String serverEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        clientOptions.setDefaultRequestTimeout(Duration.ofMillis(100));
        { var discovery = clientOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = clientOptions.addClientServerChannel("profile"); channel.enableClient(); };

        DefaultZLinkFrameworkOptions serverOptions = new DefaultZLinkFrameworkOptions();
        { var discovery = serverOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = serverOptions.addClientServerChannel("profile").enableServer(serverEndpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredServer =
                 RuntimeTestSupport.startFramework(serverOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("hello", awaitDiscoveryReply(client));
        }
    }

    @Test
    void discoveryClientServerHandlerCanCallRouteMeshClient() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String apiEndpoint = tcpEndpoint();
        String apiRouteEndpoint = tcpEndpoint();
        String playRouteEndpoint = tcpEndpoint();
        RoutingId apiRouteRid = RoutingId.from("nested-api-route");
        RoutingId playRouteRid = RoutingId.from("nested-play-route");
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        DefaultZLinkFrameworkOptions apiOptions = new DefaultZLinkFrameworkOptions();
        apiOptions.setDefaultRequestTimeout(Duration.ofMillis(200));
        { var discovery = apiOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = apiOptions.addClientServerChannel("api").enableServer(apiEndpoint);
            channel.addRequestHandler(NestedRouteApiHandler.class, String.class, String.class, "NestedApi"); };
        { var route = apiOptions.addRouteMesh("route");
            route.enableServer(apiRouteEndpoint);
            route.enableClient();
            route.setRoutingId(apiRouteRid); };

        DefaultZLinkFrameworkOptions playOptions = new DefaultZLinkFrameworkOptions();
        playOptions.setDefaultRequestTimeout(Duration.ofMillis(200));
        { var discovery = playOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var route = playOptions.addRouteMesh("route");
            route.enableServer(playRouteEndpoint);
            route.enableClient();
            route.setRoutingId(playRouteRid);
            route.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "NestedRoute"); };

        DefaultZLinkFrameworkOptions clientOptions = new DefaultZLinkFrameworkOptions();
        clientOptions.setDefaultRequestTimeout(Duration.ofMillis(200));
        { var discovery = clientOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = clientOptions.addClientServerChannel("api"); channel.enableClient(); };

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime ignoredApi =
                 RuntimeTestSupport.startFramework(apiOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredPlay =
                 RuntimeTestSupport.startFramework(playOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("route:hello", awaitNestedApiReply(client));
        }
    }

    @Test
    void discoverySpotAttachedChannelClientRequestReplySucceeds() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String apiEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);
        OutboundChannelSpot.CONTEXT.set(null);

        DefaultZLinkFrameworkOptions apiOptions = new DefaultZLinkFrameworkOptions();
        apiOptions.setDefaultRequestTimeout(Duration.ofMillis(200));
        { var discovery = apiOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = apiOptions.addClientServerChannel("api").enableServer(apiEndpoint);
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "SpotApi"); };

        DefaultZLinkFrameworkOptions playOptions = new DefaultZLinkFrameworkOptions();
        playOptions.setDefaultRequestTimeout(Duration.ofMillis(200));
        { var discovery = playOptions.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var channel = playOptions.addClientServerChannel("api"); channel.enableClient(); };
        { var mesh = playOptions.addSpotMesh("game");
            { var node = mesh;
                node.addSpotFactory(OutboundChannelSpot.class); }; };

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime ignoredApi =
                 RuntimeTestSupport.startFramework(apiOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime play =
                 RuntimeTestSupport.startFramework(playOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("hello", awaitSpotAttachedChannelReply(play));
        } finally {
            OutboundChannelSpot.CONTEXT.set(null);
        }
    }

    @Test
    void clientServerSpotRouteEgress_requestReplySucceeds() throws Exception {
        String ingressEndpoint = tcpEndpoint();
        String routeSourceEndpoint = tcpEndpoint();
        String routeTargetEndpoint = tcpEndpoint();
        String sourceSpotEndpoint = tcpEndpoint();
        String targetSpotEndpoint = tcpEndpoint();
        RoutingId targetSpotRid = RoutingId.from("spot-egress-target");
        OutboundChannelSpot.CONTEXT.set(null);

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        sourceOptions.addSpotRemoteAddressResolver(SpotEgressAddressResolver.class);
        { var channel = sourceOptions.addClientServerChannel("egress");
            channel.enableClient(ingressEndpoint);};
        { var channel = sourceOptions.addRouteMesh("route");
            channel.enableServer(routeSourceEndpoint);
            channel.enableClient(routeTargetEndpoint);
            channel.setRoutingId(RoutingId.from("spot-egress-source-route")); };
        { var mesh = sourceOptions.addSpotMesh("game");
            { var node = mesh;
                node.enableRouter(sourceSpotEndpoint)
                    .setRoutingId(RoutingId.from("spot-egress-source-node"));node.addSpotFactory(OutboundChannelSpot.class); }; };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        { var channel = targetOptions.addClientServerChannel("ingress").enableServer(ingressEndpoint);
            channel.setRoutingId(RoutingId.from("spot-egress-target-node"));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Noop"); };
        { var channel = targetOptions.addRouteMesh("route");
            channel.enableServer(routeTargetEndpoint);
            channel.enableClient(routeSourceEndpoint);
            channel.setRoutingId(SPOT_EGRESS_TARGET_ROUTE_RID); };
        { var mesh = targetOptions.addSpotMesh("game");
            { var node = mesh;
                node.enableRouter(targetSpotEndpoint)
                    .setRoutingId(SPOT_EGRESS_TARGET_NODE_RID);
                node.addSpotFactory(RemoteStateSpot.class); }; };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime target =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            target.spotManager()
                .create(RemoteStateSpot.class, targetSpotRid)
                .toCompletableFuture()
                .get(3, TimeUnit.SECONDS);
            source.spotManager()
                .create(OutboundChannelSpot.class, RoutingId.from("spot-egress-client"))
                .toCompletableFuture()
                .get(3, TimeUnit.SECONDS);

            ZLinkSpotContext context = Objects.requireNonNull(OutboundChannelSpot.CONTEXT.get());
            SpotEgressReply reply = context.outbound()
                .requestToSpot(targetSpotRid, new SpotEgressRequest("ping"))
                .timeout(Duration.ofSeconds(3))
                .submit(SpotEgressReply.class)
                .toCompletableFuture()
                .get(4, TimeUnit.SECONDS);

            assertEquals("spot-egress-target", reply.spotRid());
            assertEquals(SPOT_EGRESS_TARGET_NODE_RID.toString(), reply.nodeRid());
            assertEquals("pong:ping", reply.value());
        } finally {
            OutboundChannelSpot.CONTEXT.set(null);
        }
    }

    @Test
    @DisplayName("PUB-001 fanout delivers the same sequence to three subscribers")
    void fanout_deliversSameSequenceToThreeSubscribers() {
        String endpoint = tcpEndpoint();
        FANOUT_SEQUENCE_ONE.clear();
        FANOUT_SEQUENCE_TWO.clear();
        FANOUT_SEQUENCE_THREE.clear();

        DefaultZLinkFrameworkOptions publisherOptions = new DefaultZLinkFrameworkOptions();
        { var channel = publisherOptions.addFanoutChannel("sequence").enablePublisher(endpoint); };

        DefaultZLinkFrameworkOptions firstOptions = new DefaultZLinkFrameworkOptions();
        { var channel = firstOptions.addFanoutChannel("sequence");
            channel.enableSubscriber(endpoint);
            channel.addPublishHandler(FanoutSequenceOneHandler.class, String.class, "FanoutSequence"); };

        DefaultZLinkFrameworkOptions secondOptions = new DefaultZLinkFrameworkOptions();
        { var channel = secondOptions.addFanoutChannel("sequence");
            channel.enableSubscriber(endpoint);
            channel.addPublishHandler(FanoutSequenceTwoHandler.class, String.class, "FanoutSequence"); };

        DefaultZLinkFrameworkOptions thirdOptions = new DefaultZLinkFrameworkOptions();
        { var channel = thirdOptions.addFanoutChannel("sequence");
            channel.enableSubscriber(endpoint);
            channel.addPublishHandler(FanoutSequenceThreeHandler.class, String.class, "FanoutSequence"); };

        try (ZLinkFrameworkRuntime publisher =
                 RuntimeTestSupport.startFramework(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredFirst =
                 RuntimeTestSupport.startFramework(firstOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredSecond =
                 RuntimeTestSupport.startFramework(secondOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredThird =
                 RuntimeTestSupport.startFramework(thirdOptions, new ZLinkJavaBackendAdapterFactory())) {
            String commonSequence = publishUntilCommonFanoutSequence(publisher);

            assertTrue(FANOUT_SEQUENCE_ONE.contains(commonSequence));
            assertTrue(FANOUT_SEQUENCE_TWO.contains(commonSequence));
            assertTrue(FANOUT_SEQUENCE_THREE.contains(commonSequence));
        } finally {
            FANOUT_SEQUENCE_ONE.clear();
            FANOUT_SEQUENCE_TWO.clear();
            FANOUT_SEQUENCE_THREE.clear();
        }
    }

    @Test
    @DisplayName("PUB-001 partial manual fanout publish dispatches across hosts")
    void publisherAndSubscriber_workAcrossHosts() throws InterruptedException {
        String endpoint = tcpEndpoint();
        CountDownLatch latch = new CountDownLatch(1);
        FANOUT_LATCH.set(latch);
        FANOUT_MESSAGE.set(null);
        FANOUT_TOPIC.set(null);

        DefaultZLinkFrameworkOptions publisherOptions = new DefaultZLinkFrameworkOptions();
        { var channel = publisherOptions.addFanoutChannel("events").enablePublisher(endpoint); };

        DefaultZLinkFrameworkOptions subscriberOptions = new DefaultZLinkFrameworkOptions();
        { var channel = subscriberOptions.addFanoutChannel("events"); channel.enableSubscriber(endpoint);
            channel.addPublishHandler(ScoreChangedHandler.class, String.class, "ScoreChanged"); };

        try (ZLinkFrameworkRuntime ignoredPublisher =
                 RuntimeTestSupport.startFramework(publisherOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime subscriber =
                 RuntimeTestSupport.startFramework(subscriberOptions, new ZLinkJavaBackendAdapterFactory())) {
            publishUntilDelivered(ignoredPublisher);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "fanout publish was not delivered");
            assertEquals("home:1", FANOUT_MESSAGE.get());
            assertEquals("score", FANOUT_TOPIC.get());
            assertEquals("events", FANOUT_CHANNEL.get());
        } finally {
            FANOUT_LATCH.set(null);
            FANOUT_MESSAGE.set(null);
            FANOUT_TOPIC.set(null);
            FANOUT_CHANNEL.set(null);
        }
    }

    @Test
    void routeMesh_requestByRoutingIdSucceeds() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("source-node");
        RoutingId targetRid = RoutingId.from("target-node");
        ROUTE_REQUEST_CHANNEL.set(null);

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMesh("route"); channel.enableServer(sourceEndpoint);
            channel.setRoutingId(sourceRid);
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        { var channel = targetOptions.addRouteMesh("route"); channel.enableServer(targetEndpoint);
            channel.setRoutingId(targetRid);
            channel.enableClient(sourceEndpoint);
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime ignoredSource =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime target =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("route:hello", awaitRouteReply(ignoredSource, targetRid));
            assertEquals("route", ROUTE_REQUEST_CHANNEL.get());
        } finally {
            ROUTE_REQUEST_CHANNEL.set(null);
        }
    }

    @Test
    void routeMesh_scannedHandlerGroupRequestSucceeds() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-scanned-source");
        RoutingId targetRid = RoutingId.from("route-scanned-target");

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMesh("route"); channel.enableServer(sourceEndpoint);
            channel.setRoutingId(sourceRid);
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        targetOptions.addHandlersFromPackageOf(ChannelMessagingTest.class);
        { var channel = targetOptions.addRouteMesh("route"); channel.enableServer(targetEndpoint);
            channel.setRoutingId(targetRid);
            channel.enableClient(sourceEndpoint);
            channel.addHandlerGroup("route-shared"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("scanned-route:hello", awaitScannedRouteReply(source, targetRid));
        }
    }

    @Test
    void handlerFiltersDoNotWrapRouteMeshRequestDispatch() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-filter-source");
        RoutingId targetRid = RoutingId.from("route-filter-target");
        FILTER_REQUEST.set(null);
        FILTER_PACKET.set(null);

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMesh("route"); channel.enableServer(sourceEndpoint);
            channel.setRoutingId(sourceRid);
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        targetOptions.useFilter(ReplyDecoratingFilter.class);
        { var channel = targetOptions.addRouteMesh("route"); channel.enableServer(targetEndpoint);
            channel.setRoutingId(targetRid);
            channel.enableClient(sourceEndpoint);
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
            ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("route:hello", awaitRouteReply(source, targetRid));
            assertNull(FILTER_REQUEST.get());
            assertNull(FILTER_PACKET.get());
        } finally {
            FILTER_REQUEST.set(null);
            FILTER_PACKET.set(null);
        }
    }

    @Test
    void routeMesh_matchesRepliesByRequestSequenceWhenPacketNameIsShared() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-seq-source");
        RoutingId targetRid = RoutingId.from("route-seq-target");

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMesh("route"); channel.enableServer(sourceEndpoint);
            channel.setRoutingId(sourceRid);
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        { var channel = targetOptions.addRouteMesh("route"); channel.enableServer(targetEndpoint);
            channel.setRoutingId(targetRid);
            channel.enableClient(sourceEndpoint);
            channel.addRequestHandler(DelayedRouteEchoHandler.class, String.class, String.class, "SharedPacket"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            assertEquals("warmup", awaitSharedRouteReply(source, targetRid, "warmup:1"));

            CompletionStage<String> slow = source.route()
                .requestTo("route", targetRid, message("slow:40"))
                .packetName("SharedPacket")
                .timeout(Duration.ofSeconds(3))
                .submit(String.class);
            CompletionStage<String> fast = source.route()
                .requestTo("route", targetRid, message("fast:1"))
                .packetName("SharedPacket")
                .timeout(Duration.ofSeconds(3))
                .submit(String.class);

            assertEquals("slow", slow.toCompletableFuture().join());
            assertEquals("fast", fast.toCompletableFuture().join());
        }
    }

    @Test
    void routeMesh_sendByRoutingIdDispatchesToHandler() throws InterruptedException {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("route-send-source");
        RoutingId targetRid = RoutingId.from("route-send-target");
        CountDownLatch latch = new CountDownLatch(1);
        ROUTE_SEND_LATCH.set(latch);
        ROUTE_SEND_MESSAGE.set(null);
        ROUTE_SEND_PACKET.set(null);
        ROUTE_SEND_CHANNEL.set(null);
        ROUTE_SEND_SOURCE.set(null);

        DefaultZLinkFrameworkOptions sourceOptions = new DefaultZLinkFrameworkOptions();
        { var channel = sourceOptions.addRouteMesh("route"); channel.enableServer(sourceEndpoint);
            channel.setRoutingId(sourceRid);
            channel.enableClient(targetEndpoint); };

        DefaultZLinkFrameworkOptions targetOptions = new DefaultZLinkFrameworkOptions();
        { var channel = targetOptions.addRouteMesh("route"); channel.enableServer(targetEndpoint);
            channel.setRoutingId(targetRid);
            channel.enableClient(sourceEndpoint);
            channel.addSendHandler(RouteNoticeHandler.class, String.class, "Notice"); };

        try (ZLinkFrameworkRuntime source =
                 RuntimeTestSupport.startFramework(sourceOptions, new ZLinkJavaBackendAdapterFactory());
             ZLinkFrameworkRuntime ignoredTarget =
                 RuntimeTestSupport.startFramework(targetOptions, new ZLinkJavaBackendAdapterFactory())) {
            routeSendUntilDelivered(source, targetRid);

            assertTrue(latch.await(1, TimeUnit.SECONDS), "route mesh send was not delivered");
            assertEquals("ping", ROUTE_SEND_MESSAGE.get());
            assertEquals("Notice", ROUTE_SEND_PACKET.get());
            assertEquals("route", ROUTE_SEND_CHANNEL.get());
            assertEquals(sourceRid, ROUTE_SEND_SOURCE.get());
        } finally {
            ROUTE_SEND_LATCH.set(null);
            ROUTE_SEND_MESSAGE.set(null);
            ROUTE_SEND_PACKET.set(null);
            ROUTE_SEND_CHANNEL.set(null);
            ROUTE_SEND_SOURCE.set(null);
        }
    }

    private static String awaitDiscoveryReply(ZLinkFrameworkRuntime client) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return client.client()
                    .requestToChannel("profile", message("hello"))
                    .packetName("Echo")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("discovery request did not succeed", lastFailure);
    }

    private static String awaitNestedApiReply(ZLinkFrameworkRuntime client) {
        long deadline = System.nanoTime() + Duration.ofSeconds(4).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return client.client()
                    .requestToChannel("api", message("hello"))
                    .packetName("NestedApi")
                    .timeout(Duration.ofMillis(200))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("nested api route request did not succeed", lastFailure);
    }

    private static String awaitSpotAttachedChannelReply(ZLinkFrameworkRuntime runtime) {
        runtime.spotManager()
            .create(OutboundChannelSpot.class, RoutingId.from("outbound-channel-spot"))
            .toCompletableFuture()
            .join();
        ZLinkSpotContext context = Objects.requireNonNull(OutboundChannelSpot.CONTEXT.get());
        long deadline = System.nanoTime() + Duration.ofSeconds(4).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return context.outbound()
                    .requestToChannel("api", message("hello"))
                    .packetName("SpotApi")
                    .timeout(Duration.ofMillis(200))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = new RuntimeException(describeZlinkFailure(ex), ex);
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("SPOT attached channel request did not succeed", lastFailure);
    }

    private static String describeZlinkFailure(Throwable error) {
        Throwable current = error;
        while (current != null) {
            if (current instanceof systems.zlink.contracts.errors.ZlinkRequestException request) {
                return "request result=" + request.getResult()
                    + ", errno=" + request.getNativeErrno();
            }
            if (current instanceof systems.zlink.contracts.errors.ZlinkSubmitException submit) {
                return "submit result=" + submit.getResult()
                    + ", errno=" + submit.getNativeErrno();
            }
            current = current.getCause();
        }
        return error.toString();
    }

    private static void publishUntilDelivered(ZLinkFrameworkRuntime publisher) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && FANOUT_LATCH.get().getCount() > 0) {
            publisher.fanout()
                .publish("events", "score", message("home:1"))
                .packetName("ScoreChanged")
                .submit()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
    }

    private static String publishUntilCommonFanoutSequence(ZLinkFrameworkRuntime publisher) {
        long deadline = System.nanoTime() + Duration.ofSeconds(4).toNanos();
        int sequence = 0;
        String common = null;
        while (System.nanoTime() < deadline) {
            common = commonFanoutSequence();
            if (common != null) {
                return common;
            }
            String value = "seq:" + sequence++;
            publisher.fanout()
                .publish("sequence", "score", message(value))
                .packetName("FanoutSequence")
                .submit()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
        common = commonFanoutSequence();
        if (common != null) {
            return common;
        }
        throw new AssertionError(
            "fanout sequence was not delivered to all subscribers: first="
                + FANOUT_SEQUENCE_ONE
                + ", second="
                + FANOUT_SEQUENCE_TWO
                + ", third="
                + FANOUT_SEQUENCE_THREE);
    }

    private static String commonFanoutSequence() {
        for (String value : FANOUT_SEQUENCE_ONE) {
            if (FANOUT_SEQUENCE_TWO.contains(value) && FANOUT_SEQUENCE_THREE.contains(value)) {
                return value;
            }
        }
        return null;
    }

    private static void publishManualRegistrationUntilDelivered(
        ZLinkFrameworkRuntime publisher,
        String packetName,
        String value) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && MANUAL_REG_PUBLISH_LATCH.get().getCount() > 0) {
            publisher.fanout()
                .publish("manual-events", "manual", message(value))
                .packetName(packetName)
                .submit()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
    }

    private static void publishManualRegistrationUntilObserved(
        ZLinkFrameworkRuntime publisher,
        List<ZLinkMessageFlowEvent> observedErrors,
        String packetName) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && observedErrors.stream().noneMatch(error ->
            error.messageKind() == ZLinkDispatchMessageKind.PUBLISH
                && error.errorReason() == ZLinkDispatchErrorReason.HANDLER_MISSING
                && packetName.equals(error.packetName()))) {
            publisher.fanout()
                .publish("manual-events", "manual", message("missing-event"))
                .packetName(packetName)
                .submit()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
    }

    private static void sendUntilDelivered(ZLinkFrameworkRuntime runtime) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && SEND_LATCH.get().getCount() > 0) {
            runtime.client()
                .sendToChannel("profile", message("changed"))
                .packetName("ProfileChanged")
                .submit()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
    }

    private static String awaitRouteReply(ZLinkFrameworkRuntime source, RoutingId targetRid) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return source.route()
                    .requestTo("route", targetRid, message("hello"))
                    .packetName("Echo")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("route mesh request did not succeed", lastFailure);
    }

    private static String awaitScannedRouteReply(ZLinkFrameworkRuntime source, RoutingId targetRid) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return source.route()
                    .requestTo("route", targetRid, message("hello"))
                    .packetName("String")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("scanned route mesh request did not succeed", lastFailure);
    }

    private static String awaitSharedRouteReply(
        ZLinkFrameworkRuntime source,
        RoutingId targetRid,
        String message) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return source.route()
                    .requestTo("route", targetRid, message(message))
                    .packetName("SharedPacket")
                    .timeout(Duration.ofMillis(100))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ex) {
                lastFailure = ex;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError("shared packet route request did not succeed", lastFailure);
    }

    private static void routeSendUntilDelivered(ZLinkFrameworkRuntime source, RoutingId targetRid) {
        long deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos();
        while (System.nanoTime() < deadline && ROUTE_SEND_LATCH.get().getCount() > 0) {
            source.route()
                .sendTo("route", targetRid, message("ping"))
                .packetName("Notice")
                .submit()
                .toCompletableFuture()
                .join();
            Thread.onSpinWait();
        }
    }

    private static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + nextPort();
    }

    private static String dispatchLogLine(ZLinkMessageFlowEvent error) {
        return "dispatch-error"
            + " surface=" + error.surface()
            + " messageKind=" + error.messageKind()
            + " reason=" + error.errorReason()
            + " action=" + error.errorAction()
            + " packetName=" + error.packetName()
            + " channelName=" + error.channelName()
            + " correlationId=" + error.correlationId()
            + System.lineSeparator();
    }

    private static String waitForFileLog(Path path, String marker, int expected) throws Exception {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        while (System.nanoTime() < deadline) {
            String text = Files.readString(path, StandardCharsets.UTF_8);
            if (countOccurrences(text, marker) >= expected) {
                return text;
            }
            Thread.sleep(25);
        }
        return Files.readString(path, StandardCharsets.UTF_8);
    }

    private static boolean hasDispatchError(
        List<ZLinkMessageFlowEvent> errors,
        ZLinkDispatchMessageKind kind,
        ZLinkDispatchErrorReason reason,
        ZLinkDispatchErrorAction action,
        String packetName,
        String channelName) {
        return errors.stream().anyMatch(error ->
            error.surface() == ZLinkDispatchErrorSurface.CHANNEL
                && error.messageKind() == kind
                && error.errorReason() == reason
                && error.errorAction() == action
                && packetName.equals(error.packetName())
                && channelName.equals(error.channelName()));
    }

    private static boolean waitForManualRegistrationErrors(
        List<ZLinkMessageFlowEvent> errors)
        throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        while (System.nanoTime() < deadline) {
            if (hasDispatchError(
                    errors,
                    ZLinkDispatchMessageKind.REQUEST,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    ZLinkDispatchErrorAction.REPLY_ERROR,
                    "ManualMissingReq",
                    "manual-reg")
                && hasDispatchError(
                    errors,
                    ZLinkDispatchMessageKind.SEND,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    "ManualMissingCommand",
                    "manual-reg")
                && hasDispatchError(
                    errors,
                    ZLinkDispatchMessageKind.PUBLISH,
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    ZLinkDispatchErrorAction.DROP,
                    "ManualMissingEvent",
                    "manual-events")) {
                return true;
            }
            Thread.sleep(25);
        }
        return false;
    }

    private static int countOccurrences(String text, String marker) {
        int count = 0;
        int index = 0;
        while ((index = text.indexOf(marker, index)) >= 0) {
            count++;
            index += marker.length();
        }
        return count;
    }

    private static int nextPort() {
        for (int attempt = 0; attempt < 200; attempt++) {
            int port = NEXT_PORT.getAndIncrement();
            if (isBindable(port)) {
                return port;
            }
        }
        throw new IllegalStateException("failed to allocate tcp port");
    }

    private static String message(String value) {
        return value;
    }

    private static boolean isBindable(int port) {
        try (ServerSocket server = new ServerSocket(port)) {
            server.setReuseAddress(false);
            return true;
        } catch (IOException ignored) {
            return false;
        }
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            return request;
        }
    }

    public static final class OutboundChannelSpot implements ZLinkSpot<ZLinkActor> {
        static final AtomicReference<ZLinkSpotContext> CONTEXT = new AtomicReference<>();
        private final ZLinkSpotContext context;

        public OutboundChannelSpot(ZLinkSpotContext context) {
            this.context = context;
            CONTEXT.set(context);
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }
    }

    public static final class RemoteStateSpot implements ZLinkSpot<ZLinkActor> {
        private final ZLinkSpotContext context;

        public RemoteStateSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addPacket(RemoteStateHandler.class);
        }
    }

    public static final class RemoteStateHandler {
        @ZLinkSpotRequest
        public SpotEgressReply handle(
            RemoteStateSpot spot,
            SpotEgressRequest request) {
            return new SpotEgressReply(
                spot.context().spotRid().toString(),
                spot.context().nodeRid().toString(),
                "pong:" + request.value());
        }
    }

    public record SpotEgressRequest(String value) {
    }

    public record SpotEgressReply(
        String spotRid,
        String nodeRid,
        String value) {
    }

    public static final class SpotEgressAddressResolver
        implements ZLinkSpotRemoteAddressResolver {
        @Override
        public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(RoutingId spotRid) {
            return CompletableFuture.completedFuture(
                new ZLinkSpotRemoteAddress(
                    "route",
                    SPOT_EGRESS_TARGET_ROUTE_RID,
                    spotRid,
                    ZLinkSpotKind.USER));
        }
    }

    public static final class ThrowingRequestHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            throw new IllegalStateException("DERR-007 handler exception");
        }
    }

    public record DecodePayload(String value) {
    }

    public record JsonCodecProbe(
        String name,
        int revision,
        String optionalLabel,
        List<String> tags,
        List<JsonCodecChild> children,
        JsonCodecChild nested) {
        public JsonCodecProbe {
            tags = List.copyOf(Objects.requireNonNull(tags, "tags"));
            children = List.copyOf(Objects.requireNonNull(children, "children"));
        }
    }

    public record JsonCodecChild(String name, int rank) {
    }

    public static final class DecodeProbeHandler implements ZLinkRequestHandler<DecodePayload, String> {
        static final AtomicInteger invocations = new AtomicInteger();

        @Override
        public String handle(DecodePayload request, ZLinkRequestContext context) {
            invocations.incrementAndGet();
            return "decode:" + request.value();
        }
    }

    public static final class JsonCodecEchoHandler implements ZLinkRequestHandler<JsonCodecProbe, JsonCodecProbe> {
        @Override
        public JsonCodecProbe handle(JsonCodecProbe request, ZLinkRequestContext context) {
            return request;
        }
    }

    public static final class ManualRegistrationRequestHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            return "manual:" + request;
        }
    }

    public static final class ManualRegistrationCommandHandler implements ZLinkSendHandler<String> {
        @Override
        public void handle(String message, ZLinkSendContext context) {
            MANUAL_REG_SEND_MESSAGE.set(message);
            MANUAL_REG_SEND_PACKET.set(context.packetName().orElse(""));
            MANUAL_REG_SEND_CHANNEL.set(context.channelName().orElse(""));
            MANUAL_REG_SEND_LATCH.get().countDown();
        }
    }

    public static final class ManualRegistrationPublishHandler implements ZLinkPublishHandler<String> {
        @Override
        public void handle(String message, ZLinkPublishContext context) {
            MANUAL_REG_PUBLISH_MESSAGE.set(message);
            MANUAL_REG_PUBLISH_TOPIC.set(context.topic());
            MANUAL_REG_PUBLISH_CHANNEL.set(context.channelName().orElse(""));
            MANUAL_REG_PUBLISH_LATCH.get().countDown();
        }
    }

    public static final class ReplyDecoratingFilter implements ZLinkHandlerFilter {
        @Override
        public <T> CompletionStage<T> invokeAsync(
            ZLinkInvocationContext context,
            ZLinkNext<T> next) {
            FILTER_REQUEST.set((String) context.request().orElse(""));
            FILTER_PACKET.set(context.packetName().orElse(""));
            FILTER_CHANNEL.set(context.channelName().orElse(""));
            return next.invokeAsync().thenApply(reply -> {
                @SuppressWarnings("unchecked")
                T decorated = (T) ("filtered:" + reply);
                return decorated;
            });
        }
    }

    @ZLinkHandlerGroup("scanned-profile")
    public static final class ScannedEchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            return "scanned:" + request;
        }
    }

    public static final class ProfileChangedHandler implements ZLinkSendHandler<String> {
        @Override
        public void handle(String message, ZLinkSendContext context) {
            SEND_MESSAGE.set(message);
            SEND_PACKET.set(context.packetName().orElse(""));
            SEND_CHANNEL.set(context.channelName().orElse(""));
            SEND_LATCH.get().countDown();
                    }
    }

    @ZLinkHandlerGroup("annotated-profile")
    public static final class AnnotatedProfileHandlers {
        @ZLinkRequest(packetName = "AnnotatedEcho")
        public String echo(String request) {
            return "annotated:" + request;
        }

        @ZLinkSend(packetName = "ProfileChanged")
        public void profileChanged(String message) {
            SEND_MESSAGE.set(message);
            SEND_PACKET.set("ProfileChanged");
            SEND_LATCH.get().countDown();
        }
    }

    public static final class ScoreChangedHandler implements ZLinkPublishHandler<String> {
        @Override
        public void handle(String message, ZLinkPublishContext context) {
            FANOUT_MESSAGE.set(message);
            FANOUT_TOPIC.set(context.topic());
            FANOUT_CHANNEL.set(context.channelName().orElse(""));
            FANOUT_LATCH.get().countDown();
                    }
    }

    public static final class FanoutSequenceOneHandler implements ZLinkPublishHandler<String> {
        @Override
        public void handle(String message, ZLinkPublishContext context) {
            FANOUT_SEQUENCE_ONE.add(message);
        }
    }

    public static final class FanoutSequenceTwoHandler implements ZLinkPublishHandler<String> {
        @Override
        public void handle(String message, ZLinkPublishContext context) {
            FANOUT_SEQUENCE_TWO.add(message);
        }
    }

    public static final class FanoutSequenceThreeHandler implements ZLinkPublishHandler<String> {
        @Override
        public void handle(String message, ZLinkPublishContext context) {
            FANOUT_SEQUENCE_THREE.add(message);
        }
    }

    @ZLinkHandlerGroup("annotated-events")
    public static final class AnnotatedEventHandlers {
        @ZLinkPublish(packetName = "ScoreChanged")
        public void scoreChanged(String message) {
            FANOUT_MESSAGE.set(message);
            FANOUT_TOPIC.set("score");
            FANOUT_LATCH.get().countDown();
        }
    }

    public static final class RouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRouteRequestContext context) {
            ROUTE_REQUEST_CHANNEL.set(context.channelName().orElse(""));
            return "route:" + request;
        }
    }

    public static final class NestedRouteApiHandler implements ZLinkRequestHandler<String, String> {
        private final ZLinkRouteClient routes;

        public NestedRouteApiHandler(ZLinkRouteClient routes) {
            this.routes = routes;
        }

        @Override
        public String handle(String request, ZLinkRequestContext context) {
            return routes.requestTo("route", RoutingId.from("nested-play-route"), message(request))
                .packetName("NestedRoute")
                .timeout(Duration.ofMillis(200))
                .submit(String.class)
                .toCompletableFuture()
                .join();
        }
    }

    @ZLinkHandlerGroup("route-shared")
    public static final class ScannedRouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRouteRequestContext context) {
            return "scanned-route:" + request;
        }
    }

    public static final class DelayedRouteEchoHandler implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRouteRequestContext context) {
            String[] parts = request.split(":", 2);
            String value = parts[0];
            long delayMillis = parts.length == 2 ? Long.parseLong(parts[1]) : 0;
            return CompletableFuture.supplyAsync(
                () -> value,
                CompletableFuture.delayedExecutor(delayMillis, TimeUnit.MILLISECONDS))
                .join();
        }
    }

    public static final class RouteNoticeHandler implements ZLinkRouteSendHandler<String> {
        @Override
        public void handle(String message, ZLinkRouteSendContext context) {
            ROUTE_SEND_MESSAGE.set(message);
            ROUTE_SEND_PACKET.set(context.packetName().orElse(""));
            ROUTE_SEND_CHANNEL.set(context.channelName().orElse(""));
            ROUTE_SEND_SOURCE.set(context.routingId());
            ROUTE_SEND_LATCH.get().countDown();
                    }
    }
}
