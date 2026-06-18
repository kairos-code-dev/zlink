package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.contracts.SelfCheckMessages;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;

/** Self-check: records a kill in the store without publishing it to fanout. */
@ZLinkHandlerGroup("gameapi")
public final class KillWithoutPublishHandler
    implements ZLinkRequestHandler<
        SelfCheckMessages.KillWithoutPublishReq,
        SelfCheckMessages.KillWithoutPublishRes> {
    private final GameQuestStore store;

    public KillWithoutPublishHandler(GameQuestStore store) {
        this.store = store;
    }

    @Override
    public SelfCheckMessages.KillWithoutPublishRes handle(
        SelfCheckMessages.KillWithoutPublishReq request, ZLinkRequestContext context) {
        store.addUnpublishedKill(request.playerId(), 1);
        return new SelfCheckMessages.KillWithoutPublishRes(true);
    }
}
