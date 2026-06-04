package systems.zlink.samples.kotlin.bingo.shared.contracts

data class BingoWinner(val payload: String)

data class AuthenticatePlayerReq(val accessToken: String)

data class AuthenticatePlayerRes(
    val accepted: Boolean,
    val actorId: String,
    val displayName: String,
    val reason: String?,
)

data class EnsurePlayerActorReq(val actorId: String, val displayName: String)

data class EnsurePlayerActorRes(val actorId: String, val actorType: String)

data class MatchBingoApiReq(
    val actorId: String,
    val displayName: String,
    val mode: String,
)

data class MatchBingoApiRes(val roomId: String)

data class AllocateBingoRoomReq(val mode: String)

data class AllocateBingoRoomRes(val roomId: String)
