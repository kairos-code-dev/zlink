package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.session.GameQuestSessionRegistry;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Receives projection notifications from QuestMission and pushes them to the bound stream session. */
@ZLinkHandlerGroup("gameapi")
public final class NotifyQuestProgressHandler
    implements ZLinkRequestHandler<Messages.NotifyQuestProgressReq, Messages.NotifyQuestProgressRes> {
    private final GameQuestSessionRegistry registry;

    public NotifyQuestProgressHandler(GameQuestSessionRegistry registry) {
        this.registry = registry;
    }

    @Override
    public Messages.NotifyQuestProgressRes handle(
        Messages.NotifyQuestProgressReq request, ZLinkRequestContext context) {
        return new Messages.NotifyQuestProgressRes(registry.notify(request));
    }
}
