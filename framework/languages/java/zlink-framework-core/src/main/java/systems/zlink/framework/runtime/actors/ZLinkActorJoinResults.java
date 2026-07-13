package systems.zlink.framework.runtime.actors;

import java.util.List;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;

final class ZLinkActorJoinResults {
    private ZLinkActorJoinResults() {
    }

    static <TReply> ZLinkActorJoinResult<TReply> withReply(
        ZLinkMessageSerializer serializer,
        int joinResultCode,
        ZLinkBackendActorRef actor,
        List<Message> replyParts,
        Class<TReply> replyType) {
        Message emptyReply = null;
        try {
            if (joinResultCode != 0
                && (replyParts.isEmpty() || replyParts.get(0).size() == 0)) {
                return new ZLinkActorJoinResult.Rejected<>(null);
            }
            Message firstReply = replyParts.isEmpty()
                ? (emptyReply = Message.from(new byte[0]))
                : replyParts.get(0);
            TReply reply = ZLinkMessagePayloads.deserialize(serializer, firstReply, replyType);
            return joinResultCode == 0
                ? new ZLinkActorJoinResult.Accepted<>(
                    ZLinkActorRuntime.toPublicActorRef(actor), reply)
                : new ZLinkActorJoinResult.Rejected<>(reply);
        } finally {
            if (emptyReply != null) {
                emptyReply.close();
            }
            closeReplyParts(replyParts);
        }
    }

    static ZLinkActorJoinResult<Void> withoutReply(
        int joinResultCode,
        ZLinkBackendActorRef actor,
        List<Message> replyParts) {
        try {
            return joinResultCode == 0
                ? new ZLinkActorJoinResult.Accepted<>(
                    ZLinkActorRuntime.toPublicActorRef(actor), null)
                : new ZLinkActorJoinResult.Rejected<>(null);
        } finally {
            closeReplyParts(replyParts);
        }
    }

    private static void closeReplyParts(List<Message> replyParts) {
        replyParts.forEach(Message::close);
    }
}
