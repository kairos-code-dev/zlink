package systems.zlink.stream.connector;

import java.time.Duration;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletionStage;

final class DefaultZLinkStreamTypedRequestCall implements ZLinkStreamTypedRequestCall {
    private final ZLinkStreamRequestCall inner;
    private final ZLinkStreamTypedCodec codec;

    DefaultZLinkStreamTypedRequestCall(
        ZLinkStreamRequestCall inner,
        ZLinkStreamTypedCodec codec) {
        this.inner = Objects.requireNonNull(inner, "inner");
        this.codec = Objects.requireNonNull(codec, "codec");
    }

    @Override
    public ZLinkStreamTypedRequestCall packetName(String packetName) {
        return new DefaultZLinkStreamTypedRequestCall(inner.packetName(packetName), codec);
    }

    @Override
    public ZLinkStreamTypedRequestCall metadata(String key, String value) {
        return new DefaultZLinkStreamTypedRequestCall(inner.metadata(key, value), codec);
    }

    @Override
    public ZLinkStreamTypedRequestCall metadata(Map<String, String> metadata) {
        return new DefaultZLinkStreamTypedRequestCall(inner.metadata(metadata), codec);
    }

    @Override
    public ZLinkStreamTypedRequestCall compress() {
        return new DefaultZLinkStreamTypedRequestCall(inner.compress(), codec);
    }

    @Override
    public ZLinkStreamTypedRequestCall timeout(Duration timeout) {
        return new DefaultZLinkStreamTypedRequestCall(inner.timeout(timeout), codec);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        Objects.requireNonNull(replyType, "replyType");
        return inner.submit().thenApply(reply -> codec.decode(reply, replyType));
    }
}
