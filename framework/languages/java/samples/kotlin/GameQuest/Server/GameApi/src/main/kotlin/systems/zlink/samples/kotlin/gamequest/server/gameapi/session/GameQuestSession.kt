package systems.zlink.samples.kotlin.gamequest.server.gameapi.session

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.store.GameQuestStore

/**
 * Stream session for a single connected player. Mirrors the .NET
 * `GameQuestSession`: it dispatches subscribe / progress / sync packets to the
 * registered session packet handlers and unbinds the player on disconnect.
 */
class GameQuestSession(
    private val context: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
    private val registry: GameQuestSessionRegistry,
    private val store: GameQuestStore,
) : ZLinkSession {
    override fun context(): ZLinkSessionContext = context

    override fun onConnected() {
        System.err.printf("gamequest session connected session=%s%n", context.sessionId())
    }

    override fun onDisconnected() {
        for (unbind in registry.remove(context)) {
            store.unbindSession(unbind)
        }
    }

    override fun onError(error: ZLinkStreamError) {
        System.err.printf("gamequest session error session=%s error=%s%n", context.sessionId(), error)
    }

    override fun onDispatch(header: ZLinkStreamHeader, payload: Message) {
        System.err.printf(
            "gamequest session dispatch session=%s packet=%s codec=%s%n",
            context.sessionId(), header.packetName(), header.codec(),
        )
        val handled = ZLinkAwait.await(handlers.tryHandleAsync(context, header, payload))
        check(handled) { "Unsupported GameQuest packet '${header.packetName()}'." }
        System.err.printf(
            "gamequest session handled session=%s packet=%s%n",
            context.sessionId(), header.packetName(),
        )
    }
}
