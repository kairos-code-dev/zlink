package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);

    default CompletionStage<ZLinkActorRef> submit() {
        return submitAsync();
    }

    CompletionStage<ZLinkActorRef> submitAsync();
}
