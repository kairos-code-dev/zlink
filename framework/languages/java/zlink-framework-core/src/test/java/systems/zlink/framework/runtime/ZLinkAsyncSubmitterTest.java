package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.CloseResult;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.errors.ZLinkConfigurationException;

final class ZLinkAsyncSubmitterTest {
    @Test
    void oneWaySubmitIsSettledByRuntime() throws InterruptedException {
        BlockingPublishBackend backend = new BlockingPublishBackend();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addFanoutChannel("events").enablePublisher("inproc://events"); };

        try (ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, backend)) {
            runtime.fanout()
                .publish("events", "topic", "payload")
                .submit();

            assertTrue(backend.entered.await(1, TimeUnit.SECONDS));
            backend.release.countDown();
        }
    }

    @Test
    void submit_failsPendingItemWhenSendTimeoutExpires() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofMillis(20));
        { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile"); };

        try (ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, new NoReplyBackend())) {
            CompletionException error = org.junit.jupiter.api.Assertions.assertThrows(
                CompletionException.class,
                () -> runtime.client()
                    .requestToChannel("profile", "hello")
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());

            assertInstanceOf(TimeoutException.class, error.getCause());
        }
    }

    @Test
    void close_failsPendingItems() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofSeconds(5));
        { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile"); };

        ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, new NoReplyBackend());
        var pending = runtime.client()
            .requestToChannel("profile", "hello")
            .submit(String.class)
            .toCompletableFuture();

        runtime.close();

        CompletionException error = org.junit.jupiter.api.Assertions.assertThrows(
            CompletionException.class,
            pending::join);
        assertInstanceOf(ZLinkConfigurationException.class, error.getCause());
    }

    @Test
    void requestUsesChannelDefaultRequestTimeoutBeforeGlobalDefault() {
        RecordingRequestBackend backend = new RecordingRequestBackend();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultRequestTimeout(Duration.ofSeconds(30));
        { var channel = options.addClientServerChannel("profile");
            channel.enableClient("inproc://profile");
            channel.setDefaultRequestTimeout(Duration.ofSeconds(2)); };

        try (ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, backend)) {
            var pending = runtime.client()
                .requestToChannel("profile", "hello")
                .submit(String.class)
                .toCompletableFuture();

            assertEquals(Duration.ofSeconds(2), backend.timeout);
            runtime.close();
            pending.cancel(true);
        }
    }

    @Test
    void close_ignoresBackendCloseExceptionDuringRuntimeShutdown() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile"); };

        ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(
            options,
            new CloseFailureBackend());

        assertDoesNotThrow(runtime::close);
    }

    private static class NoReplyBackend implements ZLinkBackendAdapterProvider, ZLinkChannelBackendAdapter {
        @Override
        public ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options) {
            return this;
        }

        @Override
        public ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkMonitoringBackendAdapter createMonitoringAdapter(ZLinkBackendAdapterOptions options) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendContext createContext() {
            return new ZLinkBackendContext() {
                @Override public String name() { return "context"; }
                @Override public void shutdown() { }
                @Override public void close() { }
            };
        }

        @Override
        public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) {
            return new NoReplyDealer();
        }

        @Override
        public ZLinkBackendRouterSocket createRouterSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendPublisherSocket createPublisherSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ZLinkBackendSubscriberSocket createSubscriberSocket(ZLinkBackendContext context) {
            throw new UnsupportedOperationException();
        }
    }

    private static final class BlockingPublishBackend extends NoReplyBackend {
        private final CountDownLatch entered = new CountDownLatch(1);
        private final CountDownLatch release = new CountDownLatch(1);

        @Override
        public ZLinkBackendPublisherSocket createPublisherSocket(ZLinkBackendContext context) {
            return new BlockingPublisher(entered, release);
        }
    }

    private static final class CloseFailureBackend extends NoReplyBackend {
        @Override
        public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) {
            return new CloseFailureDealer();
        }
    }

    private static final class RecordingRequestBackend extends NoReplyBackend {
        private Duration timeout;

        @Override
        public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) {
            return new NoReplyDealer() {
                @Override public boolean request(
                    List<Message> parts,
                    ZLinkBackendRequestCallback callback,
                    SendFlags flags,
                    Duration timeout) {
                    RecordingRequestBackend.this.timeout = timeout;
                    return true;
                }
            };
        }
    }

    private static class NoReplyDealer implements ZLinkBackendDealerSocket {
        @Override public String name() { return "dealer"; }
        @Override public void bind(String endpoint) { }
        @Override public void connect(String endpoint) { }
        @Override public void disconnect(String endpoint) { }
        @Override public void setChannelName(String channelName) { }
        @Override public boolean send(List<Message> parts, SendFlags flags) { return true; }
        @Override public boolean request(
            List<Message> parts,
            ZLinkBackendRequestCallback callback,
            SendFlags flags,
            Duration timeout) {
            return true;
        }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) { return null; }
        @Override public void close() { }
    }

    private static final class CloseFailureDealer extends NoReplyDealer {
        @Override public void close() {
            throw new ZlinkCloseException(CloseResult.SHUTDOWN);
        }
    }

    private static final class BlockingPublisher implements ZLinkBackendPublisherSocket {
        private final CountDownLatch entered;
        private final CountDownLatch release;

        BlockingPublisher(CountDownLatch entered, CountDownLatch release) {
            this.entered = entered;
            this.release = release;
        }

        @Override public String name() { return "publisher"; }
        @Override public void bind(String endpoint) { }
        @Override public void setChannelName(String channelName) { }
        @Override public void setRoutingId(RoutingId routingId) { }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) {
            entered.countDown();
            try {
                return release.await(1, TimeUnit.SECONDS);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        @Override public void close() { }
    }

    @SuppressWarnings("unused")
    private static ZLinkBackendReceived received(List<Message> parts) {
        return new ZLinkBackendReceived(
            Optional.of(RoutingId.from("source")),
            Optional.empty(),
            Optional.of(1L),
            parts);
    }
}
