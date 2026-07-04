package systems.zlink.framework.actors;

import java.time.Duration;

public interface ZLinkActorJoinEntrySpotCall extends ZLinkActorYieldJoinCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);
}
