package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.contracts.SelfCheckMessages;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;

/** Self-check: deletes a single quest projection entry to test rebuild. */
@ZLinkHandlerGroup("gameapi")
public final class DeleteProjectionHandler
    implements ZLinkRequestHandler<
        SelfCheckMessages.DeleteProjectionReq,
        SelfCheckMessages.DeleteProjectionRes> {
    private final GameQuestStore store;

    public DeleteProjectionHandler(GameQuestStore store) {
        this.store = store;
    }

    @Override
    public SelfCheckMessages.DeleteProjectionRes handle(
        SelfCheckMessages.DeleteProjectionReq request, ZLinkRequestContext context) {
        store.deleteProjection(request.playerId(), request.questId());
        return new SelfCheckMessages.DeleteProjectionRes(true);
    }
}
