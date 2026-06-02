package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

final class ZLinkFrameworkFacadeTest {
    @Test
    void publicFacadeStartsFrameworkWithoutExposingBackendRuntimeTypes() {
        String endpoint = "inproc://zlink-java-facade-" + UUID.randomUUID();

        try (ZLinkFramework framework = ZLinkFramework.start(options ->
                 options.addClientServerChannel("profile", channel -> {
                     channel.enableServer(server -> server.bind(endpoint));
                     channel.enableClient(client -> client.useManualConnections(
                         endpoints -> endpoints.connect(endpoint)));
                     channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
                 }))) {
            String reply = framework.client()
                .requestToChannel("profile", "hello")
                .packetName("Echo")
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("hello", reply);
        }
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }
}
