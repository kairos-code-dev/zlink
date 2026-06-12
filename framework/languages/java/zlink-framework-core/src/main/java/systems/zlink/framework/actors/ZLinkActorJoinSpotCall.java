package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);

    default <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType) {
        return ZLinkAwait.await(submit(replyType));
    }
}
