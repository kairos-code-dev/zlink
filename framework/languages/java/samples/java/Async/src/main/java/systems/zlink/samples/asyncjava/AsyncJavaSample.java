package systems.zlink.samples.asyncjava;

import java.time.Duration;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;

public final class AsyncJavaSample {
    public static void main(String[] args) {
        FakeClient client = new FakeClient();

        CompletionStage<Void> scenario = client.sendToChannel("profile", new Warmup("42"))
            .metadata("sample", "java")
            .submitAsync()
            .thenCompose(ignored -> client.requestToChannel("profile", new GetProfile("42"))
                .timeout(Duration.ofSeconds(1))
                .submitAsync(ProfileReply.class))
            .thenAccept(reply -> {
                require("42".equals(reply.userId()), "reply user id mismatch");
                require("ready".equals(reply.status()), "reply status mismatch");
            });

        AtomicBoolean completed = new AtomicBoolean();
        scenario.whenComplete((ignored, error) -> {
            if (error != null) {
                throw new CompletionException(error);
            }
            completed.set(true);
        });
        require(completed.get(), "sample scenario did not complete");
        require(client.sent == 1, "send was not submitted");
        require(client.requests == 1, "request was not submitted");
        System.out.println("AsyncJava sample self-check passed");
    }

    private static final class FakeClient implements ZLinkClient {
        private int sent;
        private int requests;

        @Override
        public <TMessage> ZLinkSendCall sendToChannel(String channelName, TMessage message) {
            require("profile".equals(channelName), "unexpected send channel");
            require(message instanceof Warmup, "unexpected send message");
            return new FakeSendCall(() -> sent++);
        }

        @Override
        public <TMessage> ZLinkRequestCall requestToChannel(String channelName, TMessage message) {
            require("profile".equals(channelName), "unexpected request channel");
            require(message instanceof GetProfile, "unexpected request message");
            return new FakeRequestCall(() -> requests++);
        }
    }

    private static final class FakeSendCall implements ZLinkSendCall {
        private final Runnable onSubmit;
        private final Map<String, String> metadata = new HashMap<>();

        private FakeSendCall(Runnable onSubmit) {
            this.onSubmit = onSubmit;
        }

        @Override
        public ZLinkSendCall packetName(String packetName) {
            return this;
        }

        @Override
        public ZLinkSendCall metadata(String key, String value) {
            metadata.put(key, value);
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            require("java".equals(metadata.get("sample")), "metadata was not preserved");
            onSubmit.run();
            return CompletableFuture.completedFuture(null);
        }
    }

    private static final class FakeRequestCall implements ZLinkRequestCall {
        private final Runnable onSubmit;
        private Duration timeout;

        private FakeRequestCall(Runnable onSubmit) {
            this.onSubmit = onSubmit;
        }

        @Override
        public ZLinkRequestCall packetName(String packetName) {
            return this;
        }

        @Override
        public ZLinkRequestCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkRequestCall timeout(Duration timeout) {
            this.timeout = timeout;
            return this;
        }

        @Override
        public <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType) {
            require(timeout != null && !timeout.isNegative(), "timeout was not preserved");
            onSubmit.run();
            return CompletableFuture.completedFuture(replyType.cast(new ProfileReply("42", "ready")));
        }
    }

    private record Warmup(String userId) {
    }

    private record GetProfile(String userId) {
    }

    private record ProfileReply(String userId, String status) {
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
