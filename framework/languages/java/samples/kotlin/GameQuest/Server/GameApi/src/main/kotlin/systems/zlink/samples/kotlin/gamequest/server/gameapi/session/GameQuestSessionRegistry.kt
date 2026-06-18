package systems.zlink.samples.kotlin.gamequest.server.gameapi.session

import org.springframework.stereotype.Component
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.gameapi.GameApiInstanceOptions
import systems.zlink.samples.kotlin.gamequest.shared.contracts.BindQuestSessionReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestCompletedNotify
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgressNotify
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnbindQuestSessionReq

/**
 * Tracks the live stream session per player and pushes quest notifications.
 * Mirrors the .NET `GameQuestSessionRegistry`.
 */
@Component
class GameQuestSessionRegistry(
    options: GameApiInstanceOptions,
) {
    private val gate = Any()
    private val sessionsByPlayer = HashMap<String, ZLinkSessionContext>()
    private val apiName = options.apiName

    fun bind(playerId: String, context: ZLinkSessionContext): BindQuestSessionReq {
        val binding = BindQuestSessionReq(playerId, context.sessionId(), apiName)
        synchronized(gate) {
            sessionsByPlayer[playerId] = context
        }
        return binding
    }

    fun remove(context: ZLinkSessionContext): List<UnbindQuestSessionReq> {
        val removed = mutableListOf<UnbindQuestSessionReq>()
        synchronized(gate) {
            for (entry in sessionsByPlayer.entries.toList()) {
                if (entry.value === context) {
                    sessionsByPlayer.remove(entry.key)
                    removed.add(UnbindQuestSessionReq(entry.key, context.sessionId()))
                }
            }
        }
        return removed
    }

    fun notify(request: NotifyQuestProgressReq): Boolean {
        val session: ZLinkSessionContext = synchronized(gate) {
            sessionsByPlayer[request.playerId]
        } ?: return false

        try {
            for (progress in request.projection) {
                session.client()
                    .send(QuestProgressNotify(request.playerId, session.sessionId(), progress))
                    .packetName(SampleNames.ProgressPacket)
                    .await()
            }
            val completedQuestId = request.completedQuestId
            if (!completedQuestId.isNullOrBlank()) {
                val completed = request.projection.firstOrNull { it.questId == completedQuestId }
                    ?: throw IllegalStateException(
                        "Completed quest projection entry missing for $completedQuestId.",
                    )
                session.client()
                    .send(QuestCompletedNotify(request.playerId, session.sessionId(), completed, true))
                    .packetName(SampleNames.CompletedPacket)
                    .await()
            }
        } catch (error: RuntimeException) {
            remove(session)
            System.err.printf(
                "gamequest stream notify skipped because the session is gone. player=%s session=%s%n",
                request.playerId, session.sessionId(),
            )
            return false
        }
        return true
    }
}
