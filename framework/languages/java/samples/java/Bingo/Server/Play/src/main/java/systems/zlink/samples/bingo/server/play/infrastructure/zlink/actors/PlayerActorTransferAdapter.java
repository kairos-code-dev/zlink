package systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors;

import java.util.concurrent.CancellationException;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;

public final class PlayerActorTransferAdapter
    implements ZLinkActorTransferAdapter<PlayerActor> {
    @Override
    public ZLinkMessage transferOut(
        PlayerActor actor,
        CancellationToken cancellationToken) {
        throwIfCancelled(cancellationToken);
        return ZLinkMessage.of(new TransferState(
            actor.displayName(),
            actor.roomId(),
            actor.destroyAfterEntrySpotJoin(),
            actor.disconnected()));
    }

    @Override
    public PlayerActor transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken) {
        throwIfCancelled(cancellationToken);
        TransferState transferred = state.decode(TransferState.class);
        PlayerActor actor = new PlayerActor(actorId, context);
        actor.setDisplayName(transferred.displayName());
        if (transferred.roomId() != null && !transferred.roomId().isBlank()) {
            actor.joinRoom(transferred.roomId());
        }
        if (transferred.destroyAfterEntrySpotJoin()) {
            actor.markForDestroyAfterRoomLeave();
        }
        if (transferred.disconnected()) {
            actor.markDisconnected();
        }
        return actor;
    }

    private static void throwIfCancelled(CancellationToken cancellationToken) {
        if (cancellationToken.isCancellationRequested()) {
            throw new CancellationException("actor transfer was cancelled");
        }
    }

    public record TransferState(
        String displayName,
        String roomId,
        boolean destroyAfterEntrySpotJoin,
        boolean disconnected) {
    }
}
