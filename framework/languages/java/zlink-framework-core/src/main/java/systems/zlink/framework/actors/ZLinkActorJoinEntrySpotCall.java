package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);

    CompletionStage<ZLinkActorJoinResult<Void>> submit();

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);

    default CompletionStage<ZLinkActorJoinResult<Void>> yieldAsync() {
        throw new UnsupportedOperationException(
            "yieldAsync is not supported by this actor join call");
    }

    default <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> yieldAsync(Class<TReply> replyType) {
        throw new UnsupportedOperationException(
            "yieldAsync is not supported by this actor join call");
    }

    default ZLinkActorJoinResult<Void> await() {
        return ZLinkAwait.await(submit());
    }

    default <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType) {
        return ZLinkAwait.await(submit(replyType));
    }

    default ZLinkActorJoinResult<Void> yieldAwait() {
        return ZLinkAwait.await(yieldAsync());
    }

    default <TReply> ZLinkActorJoinResult<TReply> yieldAwait(Class<TReply> replyType) {
        return ZLinkAwait.await(yieldAsync(replyType));
    }
}
