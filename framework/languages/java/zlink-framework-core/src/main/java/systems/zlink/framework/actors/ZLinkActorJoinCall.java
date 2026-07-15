package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorJoinCall {
    ZLinkActorJoinCall timeout(Duration timeout);

    CompletionStage<ZLinkActorJoinResult<Void>> submit();

    default CompletionStage<ZLinkActorJoinResult<Void>> yield() {
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.yieldCurrent(submit());
    }

    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);

    default <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> yield(Class<TReply> replyType) {
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.yieldCurrent(
            submit(replyType));
    }
}
