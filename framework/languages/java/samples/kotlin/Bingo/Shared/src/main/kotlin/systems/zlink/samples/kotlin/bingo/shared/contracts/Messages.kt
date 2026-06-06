package systems.zlink.samples.kotlin.bingo.shared.contracts

import systems.zlink.framework.handlers.ZLinkPacket

data class AuthenticateReq(val accessToken: String)

data class AuthenticateRes(val actorId: String, val displayName: String)

@ZLinkPacket("AuthenticatePlayer")
data class AuthenticatePlayerReq(val accessToken: String)

data class AuthenticatePlayerRes(
    val accepted: Boolean,
    val actorId: String,
    val displayName: String,
    val reason: String?,
)

@ZLinkPacket("EnsurePlayerActor")
data class EnsurePlayerActorReq(val actorId: String, val displayName: String)

data class ActorRefSnapshot(val nodeRid: ByteArray, val actorId: String, val generation: Long)

data class EnsurePlayerActorRes(
    val actorId: String,
    val actorType: String,
    val actor: ActorRefSnapshot,
)

@ZLinkPacket("MatchBingoReq")
data class MatchBingoReq(val mode: String)

data class MatchBingoRes(val roomId: String, val state: BingoRoomState)

@ZLinkPacket("MatchBingoApiReq")
data class MatchBingoApiReq(
    val actorId: String,
    val displayName: String,
    val mode: String,
)

data class MatchBingoApiRes(val roomId: String)

@ZLinkPacket("AllocateBingoRoomReq")
data class AllocateBingoRoomReq(val actorId: String, val mode: String)

data class AllocateBingoRoomRes(val roomId: String)

@ZLinkPacket("BingoRoomJoinReq")
data class BingoRoomJoinReq(val roomId: String, val actorId: String, val displayName: String)

data class BingoRoomJoinRes(val state: BingoRoomState)

@ZLinkPacket("StartBingoGameReq")
data class StartBingoGameReq(val roomId: String)

data class StartBingoGameRes(val state: BingoRoomState)

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
