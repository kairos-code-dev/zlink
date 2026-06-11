package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);

    CompletionStage<ZLinkActorRef> submit();
}
