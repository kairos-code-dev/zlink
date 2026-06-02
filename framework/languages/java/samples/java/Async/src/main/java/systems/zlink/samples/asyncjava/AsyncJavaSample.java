package systems.zlink.samples.asyncjava;

import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class AsyncJavaSample {
    private static final CountDownLatch WARMUP_RECEIVED = new CountDownLatch(1);
    private static final AtomicReference<String> WARMUP_USER = new AtomicReference<>();

    public static void main(String[] args) throws InterruptedException {
        String endpoint = "inproc://zlink-java-async-" + UUID.randomUUID();

        try (ZLinkFramework framework = ZLinkFramework.start(options ->
            options.addClientServerChannel("profile", channel -> {
                channel.enableServer(server -> server.bind(endpoint));
                channel.enableClient(client ->
                    client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
                channel.addSendHandler(WarmupHandler.class, String.class, "Warmup");
                channel.addRequestHandler(GetProfileHandler.class, String.class, String.class, "GetProfile");
            }))) {

            CountDownLatch scenarioDone = new CountDownLatch(1);
            AtomicReference<String> replyRef = new AtomicReference<>();
            AtomicReference<Throwable> errorRef = new AtomicReference<>();

            CompletionStage<Void> scenario = framework.client()
                .sendToChannel("profile", "42")
                .packetName("Warmup")
                .metadata("sample", "java")
                .submitAsync()
                .thenCompose(ignored -> framework.client()
                    .requestToChannel("profile", "42")
                    .packetName("GetProfile")
                    .timeout(Duration.ofSeconds(1))
                    .submitAsync(String.class))
                .thenAccept(replyRef::set);

            scenario.whenComplete((ignored, error) -> {
                errorRef.set(error);
                scenarioDone.countDown();
            });

            require(scenarioDone.await(3, TimeUnit.SECONDS), "sample scenario did not complete");
            if (errorRef.get() != null) {
                throw new CompletionException(errorRef.get());
            }
            require(WARMUP_RECEIVED.await(1, TimeUnit.SECONDS), "warmup send was not delivered");
            require("42".equals(WARMUP_USER.get()), "warmup user id mismatch");
            require("42:ready".equals(replyRef.get()), "reply mismatch");
        }

        System.out.println("AsyncJava sample self-check passed");
    }

    public static final class WarmupHandler implements ZLinkSendHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String userId, ZLinkSendContext context) {
            require(context.packetName().orElseThrow().equals("Warmup"), "send packet mismatch");
            WARMUP_USER.set(userId);
            WARMUP_RECEIVED.countDown();
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
