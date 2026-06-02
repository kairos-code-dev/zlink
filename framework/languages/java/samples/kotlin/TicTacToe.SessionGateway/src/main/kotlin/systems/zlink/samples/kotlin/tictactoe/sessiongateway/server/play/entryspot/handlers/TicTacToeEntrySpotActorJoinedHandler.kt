package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers

class TicTacToeEntrySpotActorJoinedHandler {
    fun handle(actorId: String, matchId: String): String =
        "$actorId joined entry spot for $matchId"
}
