package systems.zlink.samples.supportchat.server.support.actors;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;

public final class SupportUserActorTransferAdapter
    implements ZLinkActorTransferAdapter<SupportUserActor> {
    @Override
    public ZLinkMessage transferOut(
        SupportUserActor actor,
        CancellationToken cancellationToken) {
        return ZLinkMessage.of(new TransferState(actor.displayName(), actor.role()));
    }

    @Override
    public SupportUserActor transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken) {
        TransferState transferred = state.decode(TransferState.class);
        SupportUserActor actor = new SupportUserActor(actorId, context);
        actor.setIdentity(transferred.displayName(), transferred.role());
        return actor;
    }

    public record TransferState(String displayName, String role) {
    }
}
