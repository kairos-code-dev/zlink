package systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts

import systems.zlink.framework.handlers.ZLinkPacket
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress

/**
 * Self-check wire contracts hosted on the GameApi action channel.
 *
 * The .NET sample exposes these as HTTP self-check endpoints with path
 * parameters; the Kotlin sample mirrors them as ZLink request/reply packets so
 * the client scenario can drive the same recovery and reconciliation checks.
 * The client mirrors structurally-identical data classes under its own
 * configuration package; the JSON codec matches them by packet name and field
 * layout.
 */

@ZLinkPacket("KillWithoutPublishReq")
data class KillWithoutPublishReq(val playerId: String)

data class KillWithoutPublishRes(val accepted: Boolean)

@ZLinkPacket("DeleteProjectionReq")
data class DeleteProjectionReq(val playerId: String, val questId: String)

data class DeleteProjectionRes(val deleted: Boolean)

@ZLinkPacket("RebuildProjectionReq")
data class RebuildProjectionReq(val playerId: String, val questId: String)

data class RebuildProjectionRes(val state: QuestProgress)

@ZLinkPacket("GameQuestServerAssertReq")
data class GameQuestServerAssertReq(val marker: String = "assert")
