package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorJoinCall {
    ZLinkActorJoinCall timeout(Duration timeout);

    CompletionStage<ZLinkActorJoinResult<Void>> submit();

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);
}
