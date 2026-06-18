package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.contracts.SelfCheckMessages;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;

/** Self-check: rebuilds a quest projection entry from the stored event stream. */
@ZLinkHandlerGroup("gameapi")
public final class RebuildProjectionHandler
    implements ZLinkRequestHandler<
        SelfCheckMessages.RebuildProjectionReq,
        SelfCheckMessages.RebuildProjectionRes> {
    private final GameQuestStore store;

    public RebuildProjectionHandler(GameQuestStore store) {
        this.store = store;
    }

    @Override
    public SelfCheckMessages.RebuildProjectionRes handle(
        SelfCheckMessages.RebuildProjectionReq request, ZLinkRequestContext context) {
        return new SelfCheckMessages.RebuildProjectionRes(
            store.rebuildProjection(request.playerId(), request.questId()));
    }
}
