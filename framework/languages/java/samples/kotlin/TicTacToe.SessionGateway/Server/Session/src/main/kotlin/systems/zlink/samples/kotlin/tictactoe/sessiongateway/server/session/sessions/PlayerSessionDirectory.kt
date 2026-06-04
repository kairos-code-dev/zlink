package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions

import java.util.concurrent.ConcurrentHashMap
import systems.zlink.framework.streams.ZLinkSessionContext

object PlayerSessionDirectory {
    private val sessions = ConcurrentHashMap<String, ZLinkSessionContext>()

    fun bind(actorId: String, context: ZLinkSessionContext) {
        sessions[actorId] = context
    }

    fun unbind(actorId: String, context: ZLinkSessionContext) {
        sessions.computeIfPresent(actorId) { _, existing ->
            if (existing === context) null else existing
        }
    }

    fun find(actorId: String): ZLinkSessionContext? = sessions[actorId]
}
