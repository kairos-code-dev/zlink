package systems.zlink.samples.kotlin.gamequest.client.contracts

import systems.zlink.framework.handlers.ZLinkPacket
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress

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
