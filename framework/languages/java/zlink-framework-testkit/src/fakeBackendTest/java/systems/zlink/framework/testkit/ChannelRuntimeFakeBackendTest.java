package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

final class ChannelRuntimeFakeBackendTest {
    @Test
    void manualClientServerSendAndRequestReachBackendDealer() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile-server"); };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.client()
                .sendToChannel("profile", new Greeting("hello"))
                .submit();
            String reply = runtime.client()
                .requestToChannel("profile", new Question("question"))
                .submit(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("reply", reply);
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

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
    void channelCallsUseMessageTypePacketNameByDefault() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile-server"); };
        { var route = options.addRouteMeshChannel("route"); route.enableServer("inproc://route");
            route.enableClient("inproc://route-peer"); };
        FakeZLinkBackendAdapterFactory backendFactory = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime = RuntimeTestSupport.startFramework(options, backendFactory)) {
            runtime.client()
                .sendToChannel("profile", new ProfileGreeting("hello"))
                .submit();
            runtime.client()
                .requestToChannel("profile", new ProfileQuestion("question"))
                .submit(String.class)
                .toCompletableFuture()
                .join();
            runtime.route()
                .sendToNode("route", RoutingId.from("peer"), new ProfileGreeting("hello"))
                .submit();
            runtime.route()
                .requestToNode("route", RoutingId.from("peer"), new ProfileQuestion("question"))
                .submit(String.class)
                .toCompletableFuture()
                .join();
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.dealer",
                "dealer.setChannelName.profile",
                "dealer.connect.inproc://profile-server",
                "create.router",
                "router.setChannelName.route",
                "router.connect.inproc://route-peer",
                "router.bind.inproc://route",
                "dealer.send.ProfileGreeting",
                "dealer.request.ProfileQuestion",
                "router.send.peer.ProfileGreeting",
                "router.request.peer.ProfileQuestion",
                "close.dealer",
                "close.router",
                "close.context"),
            backendFactory.calls());
    }

    public static final class ChannelMessagingFakeHandler
        implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handle(
            String request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public record ProfileGreeting(String value) {
    }

    @ZLinkPacket("Greeting")
    public record Greeting(String value) {
    }

    @ZLinkPacket("Question")
    public record Question(String value) {
    }

    @ZLinkPacket("ProfileQuestion")
    public record ProfileQuestion(String value) {
    }
}
