package systems.zlink.samples.kotlin.bingo.server.api.handlers

class MatchBingoHandler {
    fun match(playerId: String): String = "room-1:$playerId"
}
