package systems.zlink.samples.kotlin.tictactoe.shared.contracts

data class AuthenticateRes(val actorId: String)

data class CreateGameRes(val gameId: String, val playEndpoint: String, val gameName: String)

data class GameState(
    val gameId: String,
    val board: String,
    val status: String,
    val winner: String?,
    val nextTurn: String,
    val xActorId: String?,
    val oActorId: String?,
    val lastMoveActorId: String?,
    val lastMoveCell: Int?,
)

data class GameStateNotify(val state: GameState)

data class JoinGameRes(val state: GameState)

data class PlaceMarkRes(val state: GameState)

data class PlayerJoinedNotify(
    val gameId: String,
    val actorId: String,
    val mark: String,
    val state: GameState,
)
