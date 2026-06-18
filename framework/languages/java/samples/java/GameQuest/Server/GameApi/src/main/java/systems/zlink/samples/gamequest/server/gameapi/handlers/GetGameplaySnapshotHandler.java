package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("gameapi")
public final class GetGameplaySnapshotHandler
    implements ZLinkRequestHandler<Messages.GetGameplaySnapshotReq, Messages.GetGameplaySnapshotRes> {
    private final GameQuestStore store;

    public GetGameplaySnapshotHandler(GameQuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.GetGameplaySnapshotRes handle(
        Messages.GetGameplaySnapshotReq request, ZLinkRequestContext context) {
        return store.readSnapshot(request.playerId());
    }
}
