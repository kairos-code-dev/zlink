package systems.zlink.samples.tictactoe.sessiongateway.shared.actors;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.spots.ZLinkSpot;

public final class RecordingActorContext implements ZLinkActorContext {
    private final List<String> pushes = new ArrayList<>();

    @Override
    public Optional<RoutingId> spotRid() {
        return Optional.of(RoutingId.from("room-spot"));
    }

    @Override
    public boolean isJoined() {
        return true;
    }

    @Override
    public ZLinkBoundSession boundSession() {
        return new RecordingBoundSession(pushes);
    }

    @Override
    public ZLinkSpot getSpot() {
        throw new UnsupportedOperationException("not needed by sample");
    }

    @Override
    public <TSpot extends ZLinkSpot> TSpot getSpot(Class<TSpot> spotType) {
        throw new UnsupportedOperationException("not needed by sample");
    }

    public List<String> pushes() {
        return List.copyOf(pushes);
    }
}
