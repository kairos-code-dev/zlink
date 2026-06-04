package systems.zlink.samples.asyncjava;

import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class AsyncJavaSample {
    private static final CompletableFuture<String> WARMUP_USER = new CompletableFuture<>();

    public static void main(String[] args) throws Exception {
        String endpoint = "inproc://zlink-java-async-" + UUID.randomUUID();

        try (ZLinkFramework framework = ZLinkFramework.start(options ->
            options.addClientServerChannel("profile", channel -> {
                channel.enableServer(server -> server.bind(endpoint));
                channel.enableClient(client ->
                    client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
                channel.addSendHandler(WarmupHandler.class, String.class, "Warmup");
                channel.addRequestHandler(GetProfileHandler.class, String.class, String.class, "GetProfile");
            }))) {

            CompletionStage<String> scenario = framework.client()
                .sendToChannel("profile", "42")
                .packetName("Warmup")
                .metadata("sample", "java")
                .submitAsync()
                .thenCompose(ignored -> framework.client()
                    .requestToChannel("profile", "42")
                    .packetName("GetProfile")
                    .timeout(Duration.ofSeconds(1))
                    .submitAsync(String.class));

            CompletableFuture<String> replyFuture = new CompletableFuture<>();
            scenario.whenComplete((reply, error) -> {
                if (error != null) {
                    replyFuture.completeExceptionally(error);
                } else {
                    replyFuture.complete(reply);
                }
            });

            String reply = replyFuture.get(3, TimeUnit.SECONDS);
            String warmupUser = WARMUP_USER.get(1, TimeUnit.SECONDS);
            require("42".equals(warmupUser), "warmup user id mismatch");
            require("42:ready".equals(reply), "reply mismatch");
        }

        System.out.println("AsyncJava sample self-check passed");
    }

    public static final class WarmupHandler implements ZLinkSendHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String userId, ZLinkSendContext context) {
            require(context.packetName().orElseThrow().equals("Warmup"), "send packet mismatch");
            WARMUP_USER.complete(userId);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
    }

    public static final class GetProfileHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String userId, ZLinkRequestContext context) {
            require(context.packetName().orElseThrow().equals("GetProfile"), "request packet mismatch");
            return java.util.concurrent.CompletableFuture.completedFuture(userId + ":ready");
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
