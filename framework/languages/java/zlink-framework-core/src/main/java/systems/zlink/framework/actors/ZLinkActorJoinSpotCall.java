package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);

    default <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType) {
        return submitAsync(replyType);
    }

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submitAsync(Class<TReply> replyType);
}
