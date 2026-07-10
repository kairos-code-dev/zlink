package systems.zlink.framework.runtime.actors;

import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;

final class ZLinkActorHandoffPacket implements AutoCloseable {
    private final long arrivalIndex;
    private final ZLinkStreamHeader header;
    private final Message payload;
    private final ZLinkActorReplyRoute replyRoute;
    private final CompletableFuture<Optional<Message>> reply = new CompletableFuture<>();

    ZLinkActorHandoffPacket(
        long arrivalIndex,
        ZLinkStreamHeader header,
        Message payload,
        ZLinkActorReplyRoute replyRoute) {
        this.arrivalIndex = arrivalIndex;
        this.header = header;
        this.payload = Message.from(payload);
        this.replyRoute = replyRoute;
    }

    long arrivalIndex() {
        return arrivalIndex;
    }

    ZLinkStreamHeader header() {
        return header;
    }

    Message payload() {
        return payload;
    }

    ZLinkActorReplyRoute replyRoute() {
        return replyRoute;
    }

    CompletionStage<Optional<Message>> reply() {
        return reply;
    }

    void complete(Optional<Message> response) {
        reply.complete(response);
    }

    boolean fail(Throwable error) {
        return reply.completeExceptionally(error);
    }

    @Override
    public void close() {
        payload.close();
    }
}
