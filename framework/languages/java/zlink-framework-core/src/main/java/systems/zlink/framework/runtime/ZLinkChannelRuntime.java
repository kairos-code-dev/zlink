package systems.zlink.framework.runtime;

import java.time.Duration;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkChannelRuntime implements ZLinkClient, ZLinkFanoutClient, AutoCloseable {
    private final ZLinkBackendContext context;
    private final Map<String, ZLinkBackendDealerSocket> clients = new HashMap<>();
    private final Map<String, ZLinkBackendRouterSocket> servers = new HashMap<>();
    private final Map<String, ZLinkBackendPublisherSocket> publishers = new HashMap<>();
    private final Map<String, ZLinkBackendSubscriberSocket> subscribers = new HashMap<>();
    private final List<ZLinkBackendDealerSocket> manualClients = new ArrayList<>();
    private final List<ZLinkBackendRouterSocket> manualServers = new ArrayList<>();
    private final List<ZLinkBackendPublisherSocket> manualPublishers = new ArrayList<>();
    private final List<ZLinkBackendSubscriberSocket> manualSubscribers = new ArrayList<>();
    private final List<ZLinkBackendDiscovery> discoveries = new ArrayList<>();
    private final Map<String, Map<String, ChannelRequestHandlerRegistration<?, ?, ?>>> requestHandlers =
        new HashMap<>();
    private final Map<String, Map<String, ChannelPublishHandlerRegistration<?, ?>>> publishHandlers =
        new HashMap<>();
    private final ZLinkMessageSerializer serializer;
    private final Duration defaultTimeout;
    private final ExecutorService receiveExecutor = Executors.newCachedThreadPool(task -> {
        Thread thread = new Thread(task, "zlink-java-channel-runtime");
        thread.setDaemon(true);
        return thread;
    });
    private volatile boolean running = true;

    public ZLinkChannelRuntime(
        ZLinkChannelBackendAdapter backend,
        ZLinkFrameworkRegistration registration,
        ZLinkMessageSerializer serializer) {
        this.serializer = Objects.requireNonNull(serializer, "serializer");
        this.defaultTimeout = registration.defaultTimeout();
        this.context = backend.createContext();
        for (ChannelRegistration channel : registration.channels()) {
            ZLinkBackendDiscovery discovery = discoveryFor(backend, registration, channel);
            if (channel.kind() == ChannelKind.CLIENT_SERVER && channel.clientEnabled()) {
                ZLinkBackendDealerSocket dealer = backend.createDealerSocket(context);
                if (discovery == null) {
                    for (String endpoint : channel.clientManualEndpoints()) {
                        dealer.connect(endpoint);
                    }
                    manualClients.add(dealer);
                } else {
                    dealer.attachDiscovery(discovery);
                }
                clients.put(channel.name(), dealer);
            }
            if (channel.kind() == ChannelKind.CLIENT_SERVER && !channel.serverBinds().isEmpty()) {
                ZLinkBackendRouterSocket router = backend.createRouterSocket(context);
                if (discovery != null) {
                    router.attachDiscovery(discovery);
                } else {
                    manualServers.add(router);
                }
                for (String endpoint : channel.serverBinds()) {
                    router.bind(endpoint);
                }
                servers.put(channel.name(), router);
                requestHandlers.put(channel.name(), handlersByPacket(channel));
                startRequestLoop(channel.name(), router);
            }
            if (channel.kind() == ChannelKind.FANOUT && channel.publisherEnabled()) {
                ZLinkBackendPublisherSocket publisher = backend.createPublisherSocket(context);
                if (discovery != null) {
                    publisher.attachDiscovery(discovery);
                } else {
                    manualPublishers.add(publisher);
                }
                for (String endpoint : channel.publisherBinds()) {
                    publisher.bind(endpoint);
                }
                publishers.put(channel.name(), publisher);
            }
            if (channel.kind() == ChannelKind.FANOUT && channel.subscriberEnabled()) {
                ZLinkBackendSubscriberSocket subscriber = backend.createSubscriberSocket(context);
                if (discovery != null) {
                    subscriber.attachDiscovery(discovery);
                } else {
                    for (String endpoint : channel.subscriberManualEndpoints()) {
                        subscriber.connect(endpoint);
                    }
                    manualSubscribers.add(subscriber);
                }
                subscriber.setSubscription("");
                subscribers.put(channel.name(), subscriber);
                publishHandlers.put(channel.name(), publishHandlersByPacket(channel));
                startSubscribeLoop(channel.name(), subscriber);
            }
        }
    }

    private ZLinkBackendDiscovery discoveryFor(
        ZLinkChannelBackendAdapter backend,
        ZLinkFrameworkRegistration registration,
        ChannelRegistration channel) {
        if (!registration.discoveryEnabled()
            || (channel.kind() != ChannelKind.CLIENT_SERVER && channel.kind() != ChannelKind.FANOUT)) {
            return null;
        }
        ZLinkBackendDiscovery discovery = backend.createDiscovery(
            context,
            channel.kind() == ChannelKind.CLIENT_SERVER
                ? ZLinkBackendAutoConnectType.CLIENT_SERVER
                : ZLinkBackendAutoConnectType.FANOUT,
            channel.name());
        for (String endpoint : registration.registryEndpoints()) {
            discovery.connectRegistry(endpoint);
        }
        discoveries.add(discovery);
        return discovery;
    }

    @Override
    public <TMessage> ZLinkSendCall sendToChannel(String channelName, TMessage message) {
        return new SendCall(requireClient(channelName), serializer.serialize(message));
    }

    @Override
    public <TMessage> ZLinkRequestCall requestToChannel(String channelName, TMessage message) {
        return new RequestCall(requireClient(channelName), serializer.serialize(message), defaultTimeout);
    }

    @Override
    public <TMessage> ZLinkPublishCall publish(String channelName, String topic, TMessage message) {
        return new PublishCall(requirePublisher(channelName), topic, serializer.serialize(message));
    }

    @Override
    public void close() {
        running = false;
        discoveries.forEach(ZLinkBackendDiscovery::close);
        manualClients.forEach(ZLinkBackendDealerSocket::close);
        manualServers.forEach(ZLinkBackendRouterSocket::close);
        manualPublishers.forEach(ZLinkBackendPublisherSocket::close);
        manualSubscribers.forEach(ZLinkBackendSubscriberSocket::close);
        receiveExecutor.shutdownNow();
        context.close();
    }

    private ZLinkBackendDealerSocket requireClient(String channelName) {
        ZLinkBackendDealerSocket client = clients.get(channelName);
        if (client == null) {
            throw new ZLinkConfigurationException("channel client is not configured: " + channelName);
        }
        return client;
    }

    private ZLinkBackendPublisherSocket requirePublisher(String channelName) {
        ZLinkBackendPublisherSocket publisher = publishers.get(channelName);
        if (publisher == null) {
            throw new ZLinkConfigurationException("fanout publisher is not configured: " + channelName);
        }
        return publisher;
    }

    private void startRequestLoop(String channelName, ZLinkBackendRouterSocket router) {
        receiveExecutor.submit(() -> {
            while (running) {
                ZLinkBackendReceived received = router.recv(ZLinkBackendRecvMode.DONT_WAIT);
                if (received != null) {
                    dispatchRequest(channelName, router, received);
                } else {
                    Thread.onSpinWait();
                }
            }
        });
    }

    private void dispatchRequest(
        String channelName,
        ZLinkBackendRouterSocket router,
        ZLinkBackendReceived received) {
        try {
            ParsedPacket packet = parsePacket(received.parts());
            ChannelRequestHandlerRegistration<?, ?, ?> registration =
                requestHandlers.getOrDefault(channelName, Map.of()).get(packet.packetName());
            if (registration == null || received.routingId().isEmpty() || received.requestSeq().isEmpty()) {
                return;
            }
            Message reply = invokeRequestHandler(registration, packet.payload());
            router.reply(received.routingId().get(), received.requestSeq().get(), List.of(reply));
        } finally {
            received.parts().forEach(Message::close);
        }
    }

    private void startSubscribeLoop(String channelName, ZLinkBackendSubscriberSocket subscriber) {
        receiveExecutor.submit(() -> {
            while (running) {
                ZLinkBackendTopicMessage received = subscriber.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
                if (received != null) {
                    dispatchPublish(channelName, received);
                } else {
                    Thread.onSpinWait();
                }
            }
        });
    }

    private void dispatchPublish(String channelName, ZLinkBackendTopicMessage received) {
        try {
            ParsedPacket packet = parsePacket(received.parts());
            ChannelPublishHandlerRegistration<?, ?> registration =
                publishHandlers.getOrDefault(channelName, Map.of()).get(packet.packetName());
            if (registration == null) {
                return;
            }
            invokePublishHandler(registration, received.topic(), packet.payload());
        } finally {
            received.parts().forEach(Message::close);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private Message invokeRequestHandler(
        ChannelRequestHandlerRegistration registration,
        Message payload) {
        try {
            ZLinkRequestHandler handler =
                (ZLinkRequestHandler) registration.handlerType().getDeclaredConstructor().newInstance();
            Object request = serializer.deserialize(payload, registration.requestType());
            Object reply = handler.handleAsync(request, new DefaultRequestContext(registration.packetName()))
                .toCompletableFuture()
                .join();
            return serializer.serialize(reply);
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "failed to create request handler: " + registration.handlerType().getName());
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private void invokePublishHandler(
        ChannelPublishHandlerRegistration registration,
        String topic,
        Message payload) {
        try {
            ZLinkPublishHandler handler =
                (ZLinkPublishHandler) registration.handlerType().getDeclaredConstructor().newInstance();
            Object message = serializer.deserialize(payload, registration.messageType());
            handler.handleAsync(message, new DefaultPublishContext(registration.packetName(), topic))
                .toCompletableFuture()
                .join();
        } catch (ReflectiveOperationException ex) {
            throw new ZLinkConfigurationException(
                "failed to create publish handler: " + registration.handlerType().getName());
        }
    }

    private static Map<String, ChannelRequestHandlerRegistration<?, ?, ?>> handlersByPacket(
        ChannelRegistration channel) {
        Map<String, ChannelRequestHandlerRegistration<?, ?, ?>> handlers = new HashMap<>();
        for (ChannelRequestHandlerRegistration<?, ?, ?> handler : channel.requestHandlers()) {
            handlers.put(handler.packetName(), handler);
        }
        return Map.copyOf(handlers);
    }

    private static Map<String, ChannelPublishHandlerRegistration<?, ?>> publishHandlersByPacket(
        ChannelRegistration channel) {
        Map<String, ChannelPublishHandlerRegistration<?, ?>> handlers = new HashMap<>();
        for (ChannelPublishHandlerRegistration<?, ?> handler : channel.publishHandlers()) {
            handlers.put(handler.packetName(), handler);
        }
        return Map.copyOf(handlers);
    }

    private static ParsedPacket parsePacket(List<Message> parts) {
        if (parts.size() >= 2) {
            return new ParsedPacket(parts.get(0).toUtf8String(), parts.get(1));
        }
        return new ParsedPacket("", parts.get(0));
    }

    private record ParsedPacket(String packetName, Message payload) {
    }

    private static final class DefaultRequestContext implements ZLinkRequestContext {
        private static final CancellationToken NONE = () -> false;
        private final String packetName;

        DefaultRequestContext(String packetName) {
            this.packetName = packetName;
        }

        @Override
        public Optional<String> channelName() {
            return Optional.empty();
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private static final class DefaultPublishContext implements ZLinkPublishContext {
        private static final CancellationToken NONE = () -> false;
        private final String packetName;
        private final String topic;

        DefaultPublishContext(String packetName, String topic) {
            this.packetName = packetName;
            this.topic = topic;
        }

        @Override
        public String topic() {
            return topic;
        }

        @Override
        public Optional<String> source() {
            return Optional.empty();
        }

        @Override
        public Optional<String> channelName() {
            return Optional.empty();
        }

        @Override
        public Optional<String> packetName() {
            return Optional.ofNullable(packetName).filter(value -> !value.isBlank());
        }

        @Override
        public Optional<String> contentType() {
            return Optional.empty();
        }

        @Override
        public CancellationToken cancellationToken() {
            return NONE;
        }
    }

    private record PublishCall(
        ZLinkBackendPublisherSocket publisher,
        String topic,
        Message payload,
        Optional<String> packetName) implements ZLinkPublishCall {
        PublishCall(ZLinkBackendPublisherSocket publisher, String topic, Message payload) {
            this(publisher, topic, payload, Optional.empty());
        }

        @Override
        public ZLinkPublishCall packetName(String packetName) {
            return new PublishCall(publisher, topic, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkPublishCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                List<Message> publishParts = parts(packetName, payload);
                try {
                    publisher.publish(topic, publishParts, SendFlags.NONE);
                } finally {
                    publishParts.forEach(Message::close);
                }
            });
        }
    }

    private record SendCall(
        ZLinkBackendDealerSocket client,
        Message payload,
        Optional<String> packetName) implements ZLinkSendCall {
        SendCall(ZLinkBackendDealerSocket client, Message payload) {
            this(client, payload, Optional.empty());
        }

        @Override
        public ZLinkSendCall packetName(String packetName) {
            return new SendCall(client, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.runAsync(() -> {
                try {
                    client.send(parts(packetName, payload), SendFlags.NONE);
                } finally {
                    payload.close();
                }
            });
        }
    }

    private final class RequestCall implements ZLinkRequestCall {
        private final ZLinkBackendDealerSocket client;
        private final Message payload;
        private final Optional<String> packetName;
        private final Duration timeout;

        RequestCall(ZLinkBackendDealerSocket client, Message payload, Duration timeout) {
            this(client, payload, Optional.empty(), timeout);
        }

        private RequestCall(
            ZLinkBackendDealerSocket client,
            Message payload,
            Optional<String> packetName,
            Duration timeout) {
            this.client = client;
            this.payload = payload;
            this.packetName = packetName;
            this.timeout = timeout;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return new RequestCall(client, payload, Optional.of(packetName), timeout);
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            return new RequestCall(client, payload, packetName, timeout);
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            CompletableFuture<TReply> result = new CompletableFuture<>();
            try {
                client.request(parts(packetName, payload), reply -> {
                    Message emptyReply = null;
                    try {
                        Message firstReply = reply.parts().isEmpty()
                            ? (emptyReply = Message.from(new byte[0]))
                            : reply.parts().get(0);
                        result.complete(serializer.deserialize(firstReply, replyType));
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        if (emptyReply != null) {
                            emptyReply.close();
                        }
                        reply.parts().forEach(Message::close);
                    }
                }, SendFlags.NONE, timeout);
            } finally {
                payload.close();
            }
            return result;
        }
    }

    private static List<Message> parts(Optional<String> packetName, Message payload) {
        if (packetName.isEmpty()) {
            return List.of(payload);
        }
        return List.of(Message.from(packetName.get().getBytes(StandardCharsets.UTF_8)), payload);
    }
}
