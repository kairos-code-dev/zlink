package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors;

import java.util.concurrent.CancellationException;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerInfo;

public final class PlayActorTransferAdapter
    implements ZLinkActorTransferAdapter<PlayActor> {
    @Override
    public ZLinkMessage transferOut(
        PlayActor actor,
        CancellationToken cancellationToken) {
        throwIfCancelled(cancellationToken);
        return ZLinkMessage.of(new TransferState(
            actor.joinedRoomId(),
            actor.playerOrNull(),
            actor.destroyAfterEntrySpotJoin(),
            actor.disconnected()));
    }

    @Override
    public PlayActor transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken) {
        throwIfCancelled(cancellationToken);
        TransferState transferred = state.decode(TransferState.class);
        PlayActor actor = new PlayActor(actorId, context);
        if (transferred.player() != null) {
            actor.applyPlayer(transferred.player());
        }
        if (transferred.roomId() != null && !transferred.roomId().isBlank()) {
            actor.joinGame(transferred.roomId());
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
        String roomId,
        PlayerInfo player,
        boolean destroyAfterEntrySpotJoin,
        boolean disconnected) {
    }
}
