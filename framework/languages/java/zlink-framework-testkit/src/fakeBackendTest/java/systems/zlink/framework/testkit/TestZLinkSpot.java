package systems.zlink.framework.testkit;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;

public abstract class TestZLinkSpot<TActor extends ZLinkActor> implements ZLinkSpot<TActor> {
    @Override
    public void onJoinedActor(TActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(TActor actor, CancellationToken cancellationToken) {
    }
}
