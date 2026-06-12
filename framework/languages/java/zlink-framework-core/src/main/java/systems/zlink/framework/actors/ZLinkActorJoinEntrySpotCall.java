package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);

    CompletionStage<ZLinkActorRef> submit();

    default ZLinkActorRef await() {
        return ZLinkAwait.await(submit());
    }
}
