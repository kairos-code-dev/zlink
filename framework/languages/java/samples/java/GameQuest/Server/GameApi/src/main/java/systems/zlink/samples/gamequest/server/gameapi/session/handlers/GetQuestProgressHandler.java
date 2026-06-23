package systems.zlink.samples.gamequest.server.gameapi.session.handlers;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.gamequest.server.gameapi.infrastructure.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Replies with the current quest projection for the player over the stream. */
public final class GetQuestProgressHandler implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final GameQuestStore store;

    public GetQuestProgressHandler(GameQuestStore store) {
        this.store = store;
    }

    @Override
    public String packetName() {
        return "GetQuestProgressReq";
    }

    @Override
    public void handle(ZLinkSessionContext context, ZLinkStreamHeader header, ZLinkMessage payload) {
        Messages.GetQuestProgressReq request =
            payload.decode(Messages.GetQuestProgressReq.class);
        context.client()
            .reply(new Messages.GetQuestProgressRes(store.readProjection(request.playerId())))
            .await();
    }
}
