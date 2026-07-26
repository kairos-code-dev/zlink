package systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;

public final class PlayerActor implements ZLinkActor {
    private final String actorId;
    private final ZLinkActorContext context;
    private String displayName;
    private String roomId = "";
    private boolean destroyAfterEntrySpotJoin;
    private boolean disconnected;

    public PlayerActor(String actorId, ZLinkActorContext context) {
        this.actorId = actorId;
        this.context = context;
        this.displayName = actorId;
    }

    public String actorId() {
        return actorId;
    }

    @Override
    public ZLinkActorContext context() {
        return context;
    }

    public String displayName() {
        return displayName;
    }

    public void setDisplayName(String displayName) {
        this.displayName = displayName;
    }

    public void joinRoom(String roomId) {
        this.roomId = roomId;
    }

    public String roomId() {
        return roomId;
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

    public void push(Object message) {
        context.boundSession()
            .send(message)
            .submit();
    }
}
