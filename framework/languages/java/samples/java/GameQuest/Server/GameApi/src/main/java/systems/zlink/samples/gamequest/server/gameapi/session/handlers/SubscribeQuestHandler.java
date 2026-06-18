package systems.zlink.samples.gamequest.server.gameapi.session.handlers;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.gamequest.server.gameapi.session.GameQuestSessionRegistry;
import systems.zlink.samples.gamequest.server.gameapi.session.StreamPayloads;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Binds the player session and replies with the current quest projection. */
public final class SubscribeQuestHandler implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final GameQuestStore store;
    private final GameQuestSessionRegistry registry;

    public SubscribeQuestHandler(GameQuestStore store, GameQuestSessionRegistry registry) {
        this.store = store;
        this.registry = registry;
    }

    @Override
    public String packetName() {
        return "SubscribeQuestReq";
    }

    @Override
    public void handle(ZLinkSessionContext context, ZLinkStreamHeader header, Message payload) {
        Messages.SubscribeQuestReq request =
            StreamPayloads.decode(header, payload, Messages.SubscribeQuestReq.class);
        System.err.printf(
            "gamequest session subscribe session=%s player=%s%n",
            context.sessionId(), request.playerId());
        store.bindSession(registry.bind(request.playerId(), context));
        System.err.printf(
            "gamequest session binding stored session=%s player=%s%n",
            context.sessionId(), request.playerId());
        context.client()
            .reply(new Messages.SubscribeQuestRes(store.readProjection(request.playerId())))
            .await();
        System.err.printf(
            "gamequest session subscribed session=%s player=%s%n",
            context.sessionId(), request.playerId());
    }
}
