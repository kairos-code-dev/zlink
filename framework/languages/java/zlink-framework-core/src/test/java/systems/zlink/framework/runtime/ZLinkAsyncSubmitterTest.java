package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;

import systems.zlink.framework.runtime.backend.*;

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
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.errors.ZLinkConfigurationException;

final class ZLinkAsyncSubmitterTest {
    @Test
    void submit_drainsPendingItemFromReadyCallback() throws InterruptedException {
        BlockingPublishBackend backend = new BlockingPublishBackend();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addFanoutChannel("events", channel ->
            channel.enablePublisher(publisher -> publisher.bind("inproc://events")));

        try (ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, backend)) {
            var submitted = runtime.fanout()
                .publish("events", "topic", "payload")
                .packetName("Event")
                .submit();

            assertTrue(backend.entered.await(1, TimeUnit.SECONDS));
            assertFalse(submitted.toCompletableFuture().isDone());
            backend.release.countDown();
            submitted.toCompletableFuture().join();
        }
    }

    @Test
    void submit_failsPendingItemWhenSendTimeoutExpires() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultTimeout(Duration.ofMillis(20));
        options.addClientServerChannel("profile", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect("inproc://profile"))));

        try (ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, new NoReplyBackend())) {
            CompletionException error = org.junit.jupiter.api.Assertions.assertThrows(
                CompletionException.class,
                () -> runtime.client()
                    .requestToChannel("profile", "hello")
                    .packetName("Echo")
                    .submit(String.class)
                    .toCompletableFuture()
                    .join());

            assertInstanceOf(TimeoutException.class, error.getCause());
        }
    }

    @Test
    void close_failsPendingItems() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultTimeout(Duration.ofSeconds(5));
        options.addClientServerChannel("profile", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect("inproc://profile"))));

        ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, new NoReplyBackend());
        var pending = runtime.client()
            .requestToChannel("profile", "hello")
            .packetName("Echo")
            .submit(String.class)
            .toCompletableFuture();

        runtime.close();

        CompletionException error = org.junit.jupiter.api.Assertions.assertThrows(
            CompletionException.class,
            pending::join);
        assertInstanceOf(ZLinkConfigurationException.class, error.getCause());
    }

    private static class NoReplyBackend implements ZLinkBackendAdapterFactory, ZLinkChannelBackendAdapter {
        @Override
        public ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options) {
            return this;
        }

        @Override
        public ZLinkRegistryBackendAdapter createRegistryAdapter(ZLinkBackendAdapterOptions options) {
            throw new UnsupportedOperationException();
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
        public ZLinkBackendDiscovery createDiscovery(
            ZLinkBackendContext context,
            ZLinkBackendAutoConnectType autoConnectType,
            String channelName) {
            throw new UnsupportedOperationException();
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

    private static final class NoReplyDealer implements ZLinkBackendDealerSocket {
        @Override public String name() { return "dealer"; }
        @Override public void bind(String endpoint) { }
        @Override public void connect(String endpoint) { }
        @Override public void disconnect(String endpoint) { }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { }
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

    private static final class BlockingPublisher implements ZLinkBackendPublisherSocket {
        private final CountDownLatch entered;
        private final CountDownLatch release;

        BlockingPublisher(CountDownLatch entered, CountDownLatch release) {
            this.entered = entered;
            this.release = release;
        }

        @Override public String name() { return "publisher"; }
        @Override public void bind(String endpoint) { }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { }
        @Override public void setChannelName(String channelName) { }
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
