package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

final class ChannelRuntimeFakeBackendTest {
    @Test
    void manualClientServerSendAndRequestReachBackendDealer() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addClientServerChannel("profile", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://profile-server"))));
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(options, backendFactory)) {
            runtime.client()
                .sendToChannel("profile", "hello")
                .packetName("Greeting")
                .submitAsync()
                .toCompletableFuture()
                .join();
            String reply = runtime.client()
                .requestToChannel("profile", "question")
                .packetName("Question")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("reply", reply);
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.dealer",
                "dealer.setChannelName.profile",
                "dealer.connect.inproc://profile-server",
                "dealer.send.Greeting",
                "dealer.request.Question",
                "close.dealer",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void discoveryClientServerAttachesDealerAndRouterToRegistryDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery(registry -> registry.add("tcp://127.0.0.1:5552"));
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("tcp://127.0.0.1:7100"));
            channel.enableClient();
            channel.addRequestHandler(
                ChannelMessagingFakeHandler.class,
                String.class,
                String.class,
                "Question");
        });
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored = ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.discovery.profile",
                "discovery.profile.connectRegistry.tcp://127.0.0.1:5552",
                "create.dealer",
                "dealer.setChannelName.profile",
                "dealer.attachDiscovery.discovery.profile",
                "create.router",
                "router.setChannelName.profile",
                "router.attachDiscovery.discovery.profile",
                "router.bind.tcp://127.0.0.1:7100",
                "close.discovery.profile",
                "close.context"),
            backendFactory.calls());
    }

    public static final class ChannelMessagingFakeHandler
        implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(
            String request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }
}
