package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);

    CompletionStage<ZLinkActorJoinResult<Void>> submit();

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);

    default ZLinkActorJoinResult<Void> yield() {
        throw new UnsupportedOperationException(
            "yield is not supported by this actor join call");
    }

    default ZLinkActorJoinResult<Void> yield(CancellationToken cancellationToken) {
        throw new UnsupportedOperationException(
            "yield is not supported by this actor join call");
    }

    default <TReply> ZLinkActorJoinResult<TReply> yield(Class<TReply> replyType) {
        throw new UnsupportedOperationException(
            "yield is not supported by this actor join call");
    }

    default <TReply> ZLinkActorJoinResult<TReply> yield(
        Class<TReply> replyType,
        CancellationToken cancellationToken) {
        throw new UnsupportedOperationException(
            "yield is not supported by this actor join call");
    }

    default ZLinkActorJoinResult<Void> await() {
        return ZLinkAwait.await(submit());
    }

    default <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType) {
        return ZLinkAwait.await(submit(replyType));
    }

}
