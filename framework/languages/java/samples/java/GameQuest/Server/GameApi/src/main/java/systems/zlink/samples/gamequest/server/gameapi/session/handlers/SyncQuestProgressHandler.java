package systems.zlink.samples.gamequest.server.gameapi.session.handlers;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.server.gameapi.session.StreamPayloads;
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
    public void handle(ZLinkSessionContext context, ZLinkStreamHeader header, Message payload) {
        Messages.SyncQuestProgressReq request =
            StreamPayloads.decode(header, payload, Messages.SyncQuestProgressReq.class);
        context.client().reply(actions.sync(request.playerId())).await();
    }
}
