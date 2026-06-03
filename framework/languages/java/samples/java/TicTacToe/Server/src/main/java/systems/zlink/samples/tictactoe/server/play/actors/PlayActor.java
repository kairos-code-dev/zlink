package systems.zlink.samples.tictactoe.server.play.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class PlayActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;
    private String joinedGameId;

    public PlayActor(String actorId, ZLinkActorContext context) {
        this.actorId = actorId;
        this.context = context;
    }

    @Override
    public String actorId() {
        return actorId;
    }

    @Override
    public ZLinkActorContext context() {
        return context;
    }

    public void joinGame(String gameId) {
        this.joinedGameId = gameId;
    }

    public String requireJoinedGame() {
        if (joinedGameId == null || joinedGameId.isEmpty()) {
            throw new IllegalStateException("actor has not joined a game");
        }
        return joinedGameId;
    }
}
