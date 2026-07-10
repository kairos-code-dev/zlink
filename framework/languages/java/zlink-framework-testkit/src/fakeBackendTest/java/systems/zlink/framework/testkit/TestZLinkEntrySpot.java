package systems.zlink.framework.testkit;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpot;

public abstract class TestZLinkEntrySpot<TActor extends ZLinkActor>
    implements ZLinkEntrySpot<TActor> {
    @Override
    public void onJoinedActor(TActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(TActor actor, CancellationToken cancellationToken) {
    }
}
