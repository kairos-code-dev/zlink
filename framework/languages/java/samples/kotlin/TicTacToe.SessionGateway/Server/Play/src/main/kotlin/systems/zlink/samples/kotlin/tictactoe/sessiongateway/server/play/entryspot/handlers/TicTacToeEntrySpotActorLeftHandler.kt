package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers

class TicTacToeEntrySpotActorLeftHandler {
    fun handle(actorId: String, matchId: String): String =
        "$actorId left entry spot for $matchId"
}
