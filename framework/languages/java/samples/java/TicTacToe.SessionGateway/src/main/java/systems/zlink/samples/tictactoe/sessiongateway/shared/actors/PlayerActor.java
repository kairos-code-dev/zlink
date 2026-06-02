package systems.zlink.samples.tictactoe.sessiongateway.shared.actors;

import java.util.List;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class PlayerActor implements ZLinkActor {
    private final String actorId;
    private final RecordingActorContext context = new RecordingActorContext();

    public PlayerActor(String actorId) {
        this.actorId = actorId;
    }

    @Override
    public String actorId() {
        return actorId;
    }

    @Override
    public ZLinkActorContext context() {
        return context;
    }

    public List<String> pushes() {
        return context.pushes();
    }
}
