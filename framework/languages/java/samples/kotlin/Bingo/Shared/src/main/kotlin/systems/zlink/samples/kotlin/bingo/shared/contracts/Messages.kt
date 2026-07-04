package systems.zlink.samples.kotlin.bingo.shared.contracts

import systems.zlink.framework.actors.ZLinkActorRefSnapshot
import systems.zlink.framework.handlers.ZLinkPacket

@ZLinkPacket("AuthenticateReq")
data class AuthenticateReq(val accessToken: String)

data class AuthenticateRes(val actorId: String, val displayName: String, val actorNodeRid: String)

@ZLinkPacket("AuthenticatePlayer")
data class AuthenticatePlayerReq(val accessToken: String)

data class AuthenticatePlayerRes(
    val accepted: Boolean,
    val actorId: String,
    val displayName: String,
    val reason: String?,
)

@ZLinkPacket("EnsurePlayerActor")
data class EnsurePlayerActorReq(
    val actorId: String,
    val displayName: String,
    val preferredActorNodeRid: String,
)

data class EnsurePlayerActorRes(
    val actorId: String,
    val actorType: String,
    val actor: ZLinkActorRefSnapshot,
)

@ZLinkPacket("MatchBingoReq")
data class MatchBingoReq(val mode: String)

data class MatchBingoRes(val roomId: String, val state: BingoRoomState, val roomOwnerNodeRid: String)

@ZLinkPacket("MatchBingoApiReq")
data class MatchBingoApiReq(
    val actorId: String,
    val displayName: String,
    val mode: String,
    val actorNodeRid: String,
)

data class MatchBingoApiRes(val roomId: String, val roomOwnerNodeRid: String)

@ZLinkPacket("AllocateBingoRoomReq")
data class AllocateBingoRoomReq(
    val actorId: String,
    val mode: String,
    val preferredOwnerNodeRid: String,
)

data class AllocateBingoRoomRes(val roomId: String, val roomOwnerNodeRid: String)

@ZLinkPacket("BingoRoomJoinReq")
data class BingoRoomJoinReq(
    val roomId: String,
    val actorId: String,
    val displayName: String,
    val observeOnly: Boolean,
)

data class BingoRoomJoinRes(val state: BingoRoomState)

@ZLinkPacket("SubmitBingoCardReq")
data class SubmitBingoCardReq(val roomId: String, val card: List<Int>)

data class SubmitBingoCardRes(val state: BingoRoomState)

data class PlayerJoinedNotify(
    val roomId: String,
    val actorId: String,
    val displayName: String,
    val seat: Int,
    val isHost: Boolean,
    val state: BingoRoomState,
)

data class BingoGameStartedNotify(val state: BingoRoomState)

data class BingoNumberDrawnNotify(
    val roomId: String,
    val drawSeq: Int,
    val number: Int,
    val state: BingoRoomState,
)

data class BingoStateNotify(val state: BingoRoomState)

data class BingoGameEndedNotify(val state: BingoRoomState)

@ZLinkPacket("ObserveBingoEventsReq")
data class ObserveBingoEventsReq(val roomId: String)

data class ObserveBingoEventsRes(val subscribed: Boolean, val observerNodeRid: String)

@ZLinkPacket("StopObservingBingoEventsReq")
data class StopObservingBingoEventsReq(val roomId: String)

data class StopObservingBingoEventsRes(val stopped: Boolean, val observerNodeRid: String)

data class BingoWinnerMsg(
    val roomId: String,
    val actorId: String,
    val drawSeq: Int,
    val itemId: String,
    val itemName: String,
    val rarity: String,
)

data class BingoRewardAnnouncedNotify(
    val roomId: String,
    val actorId: String,
    val drawSeq: Int,
    val itemId: String,
    val itemName: String,
    val rarity: String,
    val receivingSpotNodeRid: String,
)

data class BingoWinner(val actorId: String)

data class BingoRoomState(
    val roomId: String,
    val status: String,
    val hostActorId: String,
    val canStart: Boolean,
    val drawSeq: Int,
    val lastDrawnNumber: Int?,
    val drawnNumbers: List<Int>,
    val players: List<BingoPlayerState>,
    val winners: List<String>,
)

data class BingoPlayerState(
    val actorId: String,
    val displayName: String,
    val seat: Int,
    val isHost: Boolean,
    val card: List<Int>,
    val marks: List<Boolean>,
    val completedLines: Int,
)
