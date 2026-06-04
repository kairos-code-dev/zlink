package systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers

class BingoEntrySpotActorJoinedHandler {
    fun handle(actorId: String): String =
        "$actorId joined bingo entry spot"
}
