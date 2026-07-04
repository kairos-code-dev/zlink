package systems.zlink.framework.actors;

import java.time.Duration;

public interface ZLinkActorJoinSpotCall extends ZLinkActorYieldJoinCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);
}
