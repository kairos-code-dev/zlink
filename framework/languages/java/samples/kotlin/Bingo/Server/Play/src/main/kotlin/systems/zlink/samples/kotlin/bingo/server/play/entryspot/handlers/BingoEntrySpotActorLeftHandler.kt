package systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers

class BingoEntrySpotActorLeftHandler {
    fun handle(actorId: String): String =
        "$actorId left bingo entry spot"
}
