package systems.zlink.samples.gamequest.server.gameapi.session;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.samples.gamequest.server.gameapi.infrastructure.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * Stream session for a single connected player. Mirrors the .NET
 * {@code GameQuestSession}: it dispatches subscribe / progress / sync packets to
 * the registered session packet handlers and unbinds the player on disconnect.
 */
public final class GameQuestSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
    private final GameQuestSessionRegistry registry;
    private final GameQuestStore store;

    public GameQuestSession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
        GameQuestSessionRegistry registry,
        GameQuestStore store) {
        this.context = context;
        this.handlers = handlers;
        this.registry = registry;
        this.store = store;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public void onConnected() {
        System.err.printf("gamequest session connected session=%s%n", context.sessionId());
    }

    @Override
    public void onDisconnected() {
        for (Messages.UnbindQuestSessionReq unbind : registry.remove(context)) {
            store.unbindSession(unbind);
        }
    }

    @Override
    public void onError(ZLinkStreamError error) {
        System.err.printf("gamequest session error session=%s error=%s%n", context.sessionId(), error);
    }

    @Override
    public void onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
        System.err.printf(
            "gamequest session dispatch session=%s packet=%s%n",
            context.sessionId(), dispatch.packetName());
        boolean handled = await(handlers.tryHandleAsync(context, dispatch, payload));
        if (!handled) {
            throw new IllegalStateException("Unsupported GameQuest packet '" + dispatch.packetName() + "'.");
        }
        System.err.printf(
            "gamequest session handled session=%s packet=%s%n",
            context.sessionId(), dispatch.packetName());
    }
}
