package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorRequestCall {
    ZLinkActorRequestCall timeout(Duration timeout);

    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);

    default <TReply> CompletionStage<TReply> yield(Class<TReply> replyType) {
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.yieldCurrent(
            submit(replyType));
    }

}
