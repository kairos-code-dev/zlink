package systems.zlink.framework.runtime.backend;

import java.util.List;
import java.util.Optional;
import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public record ZLinkBackendReceived(
    ZLinkBackendRequestResult result,
    Optional<RoutingId> routingId,
    Optional<String> spotId,
    Optional<Long> requestSeq,
    byte[] applicationMetadata,
    byte[] acceptedJournalRecord,
    List<Message> parts,
    Consumer<List<Message>> reply,
    Runnable closeAction) implements AutoCloseable {
    public ZLinkBackendReceived {
        result = result == null ? ZLinkBackendRequestResult.OK : result;
        applicationMetadata =
            applicationMetadata == null ? new byte[0] : applicationMetadata.clone();
        acceptedJournalRecord = acceptedJournalRecord == null
            ? new byte[0]
            : acceptedJournalRecord.clone();
    }

    public ZLinkBackendReceived(
        ZLinkBackendRequestResult result,
        Optional<RoutingId> routingId,
        Optional<String> spotId,
        Optional<Long> requestSeq,
        byte[] applicationMetadata,
        List<Message> parts,
        Consumer<List<Message>> reply,
        Runnable closeAction) {
        this(
            result,
            routingId,
            spotId,
            requestSeq,
            applicationMetadata,
            new byte[0],
            parts,
            reply,
            closeAction);
    }

    public ZLinkBackendReceived(
        ZLinkBackendRequestResult result,
        Optional<RoutingId> routingId,
        Optional<String> spotId,
        Optional<Long> requestSeq,
        List<Message> parts,
        Consumer<List<Message>> reply,
        Runnable closeAction) {
        this(
            result,
            routingId,
            spotId,
            requestSeq,
            new byte[0],
            new byte[0],
            parts,
            reply,
            closeAction);
    }

    public ZLinkBackendReceived(
        Optional<RoutingId> routingId,
        Optional<String> spotId,
        Optional<Long> requestSeq,
        List<Message> parts) {
        this(
            ZLinkBackendRequestResult.OK,
            routingId,
            spotId,
            requestSeq,
            new byte[0],
            new byte[0],
            parts,
            null,
            () -> { });
    }

    public ZLinkBackendReceived(
        Optional<RoutingId> routingId,
        Optional<String> spotId,
        Optional<Long> requestSeq,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        this(
            ZLinkBackendRequestResult.OK,
            routingId,
            spotId,
            requestSeq,
            new byte[0],
            new byte[0],
            parts,
            reply,
            () -> { });
    }

    public ZLinkBackendReceived(
        Optional<RoutingId> routingId,
        Optional<String> spotId,
        Optional<Long> requestSeq,
        List<Message> parts,
        Consumer<List<Message>> reply,
        Runnable closeAction) {
        this(
            ZLinkBackendRequestResult.OK,
            routingId,
            spotId,
            requestSeq,
            new byte[0],
            new byte[0],
            parts,
            reply,
            closeAction);
    }

    public ZLinkBackendReceived(
        ZLinkBackendRequestResult result,
        Optional<RoutingId> routingId,
        Optional<String> spotId,
        Optional<Long> requestSeq,
        List<Message> parts) {
        this(
            result,
            routingId,
            spotId,
            requestSeq,
            new byte[0],
            new byte[0],
            parts,
            null,
            () -> { });
    }

    @Override
    public byte[] applicationMetadata() {
        return applicationMetadata.clone();
    }

    @Override
    public byte[] acceptedJournalRecord() {
        return acceptedJournalRecord.clone();
    }

    public void reply(List<Message> replyParts) {
        if (reply == null) {
            throw new IllegalStateException("received message has no reply path");
        }
        reply.accept(replyParts);
    }

    @Override
    public void close() {
        parts.forEach(Message::close);
        closeAction.run();
    }
}
