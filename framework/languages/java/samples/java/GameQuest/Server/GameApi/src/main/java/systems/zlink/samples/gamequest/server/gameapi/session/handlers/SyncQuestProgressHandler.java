package systems.zlink.samples.gamequest.server.gameapi.session.handlers;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Triggers a QuestMission reconciliation sync and replies over the stream. */
public final class SyncQuestProgressHandler implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final GameplayActionService actions;

    public SyncQuestProgressHandler(GameplayActionService actions) {
        this.actions = actions;
    }

    @Override
    public String packetName() {
        return "SyncQuestProgressReq";
    }

    @Override
    public void handle(ZLinkSessionContext context, ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
        Messages.SyncQuestProgressReq request =
            payload.decode(Messages.SyncQuestProgressReq.class);
        context.client().reply(actions.sync(request.playerId())).await();
    }
}
