package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);

    CompletionStage<ZLinkActorJoinResult<Void>> submit();

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);

    default ZLinkActorJoinResult<Void> await() {
        return ZLinkAwait.await(submit());
    }

    default <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType) {
        return ZLinkAwait.await(submit(replyType));
    }
}
