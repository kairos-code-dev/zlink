package systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;

public final class PlayActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(
        String actorId,
        ZLinkActorContext context) {
        return new PlayActor(actorId, context);
    }
}
