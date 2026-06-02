package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.ZLinkFrameworkRuntime;

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
            java.util.List.of(
                "factory.channel",
                "create.context",
                "create.dealer",
                "dealer.connect.inproc://profile-server",
                "dealer.send.Greeting",
                "dealer.request.Question",
                "close.dealer",
                "close.context"),
            backendFactory.calls());
    }
}
