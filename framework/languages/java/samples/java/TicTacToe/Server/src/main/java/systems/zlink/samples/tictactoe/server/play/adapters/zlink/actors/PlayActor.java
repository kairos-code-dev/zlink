package systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerInfo;

public final class PlayActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;
    private PlayerInfo player;
    private String joinedRoomId;
    private boolean destroyAfterEntrySpotJoin;
    private boolean disconnected;

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

    public void applyPlayer(PlayerInfo player) {
        if (!actorId.equals(player.actorId())) {
            throw new IllegalArgumentException("player actor id does not match actor");
        }
        this.player = player;
    }

    public PlayerInfo requirePlayer() {
        if (player == null) {
            throw new IllegalStateException("actor has not been authenticated");
        }
        return player;
    }

    public int incrementWins() {
        PlayerInfo current = requirePlayer();
        PlayerInfo updated = new PlayerInfo(
            current.actorId(),
            current.displayName(),
            current.level(),
            current.wins() + 1);
        this.player = updated;
        return updated.wins();
    }

    public void joinGame(String roomId) {
        this.joinedRoomId = roomId;
    }

    public String requireJoinedGame() {
        if (joinedRoomId == null || joinedRoomId.isEmpty()) {
            throw new IllegalStateException("actor has not joined a room");
        }
        return joinedRoomId;
    }

    public boolean destroyAfterEntrySpotJoin() {
        return destroyAfterEntrySpotJoin;
    }

    public void markForDestroyAfterRoomLeave() {
        destroyAfterEntrySpotJoin = true;
    }

    public boolean disconnected() {
        return disconnected;
    }

    public void markDisconnected() {
        disconnected = true;
    }
}
