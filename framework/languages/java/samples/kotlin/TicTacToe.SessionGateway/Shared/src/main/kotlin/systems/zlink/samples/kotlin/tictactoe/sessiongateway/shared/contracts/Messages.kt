package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.contracts

data class AuthenticateActorReq(val accessToken: String)
data class AuthenticateActorRes(val actorId: String)
data class CreateMatchReq(val ownerActorId: String? = null)
data class CreateMatchRes(val matchId: String, val ownerActorId: String)
data class JoinMatchReq(val matchId: String)
data class JoinMatchRes(
    val matchId: String,
    val actorId: String,
    val mark: String,
    val state: TicTacToeState,
)
data class PlaceMarkReq(val matchId: String, val cell: Int)
data class PlaceMarkRes(val state: TicTacToeState)
data class OpponentJoinedNotify(
    val matchId: String,
    val opponentActorId: String,
    val mark: String,
    val state: TicTacToeState,
)
data class TurnChangedNotify(
    val matchId: String,
    val turnActorId: String,
    val state: TicTacToeState,
)
data class GameEndedNotify(
    val matchId: String,
    val winnerActorId: String?,
    val draw: Boolean,
    val state: TicTacToeState,
)
data class GameStateChanged(val state: String)
data class TicTacToeState(
    val matchId: String,
    val board: String,
    val status: String,
    val turnActorId: String?,
    val winnerActorId: String?,
    val draw: Boolean,
    val xActorId: String?,
    val oActorId: String?,
    val lastMoveActorId: String?,
    val lastMoveCell: Int?,
)
