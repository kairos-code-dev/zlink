package systems.zlink.samples.kotlin.tictactoe.shared.contracts

data class AuthenticatePlayerReq(val accessToken: String)

data class AuthenticatePlayerRes(val actorId: String)

data class AuthenticateRes(val actorId: String)

data class AuthenticateReq(val accessToken: String)

data class CreateGameReq(val gameName: String)

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

data class JoinGameReq(val gameId: String)

data class PlaceMarkRes(val state: GameState)

data class PlaceMarkReq(val cell: Int)

data class PlayerJoinedNotify(
    val gameId: String,
    val actorId: String,
    val mark: String,
    val state: GameState,
)
