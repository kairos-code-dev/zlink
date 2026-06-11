package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);
}
