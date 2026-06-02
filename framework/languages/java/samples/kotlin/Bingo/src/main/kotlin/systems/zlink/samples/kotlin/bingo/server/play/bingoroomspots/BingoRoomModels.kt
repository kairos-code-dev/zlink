package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

data class BingoWinner(val playerId: String)
data class BingoRoomCreated(val roomId: String)
data class BingoRoomMembership(val roomId: String, val actorId: String, val joined: Boolean)
