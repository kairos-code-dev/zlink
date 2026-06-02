package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.contracts

data class AuthenticateActorReq(val accessToken: String)
data class AuthenticateActorRes(val actorId: String)
data class CreateMatchReq(val actorId: String)
data class CreateMatchRes(val matchId: String)
data class GameStateChanged(val state: String)
