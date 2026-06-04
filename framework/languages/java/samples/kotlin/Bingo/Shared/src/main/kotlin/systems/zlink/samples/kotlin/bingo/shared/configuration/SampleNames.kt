package systems.zlink.samples.kotlin.bingo.shared.configuration

object SampleNames {
    const val ApiChannel: String = "bingo.api"
    const val PlayChannel: String = "bingo.play"
    const val StreamNode: String = "bingo.client.stream"
    const val SessionSpotNode: String = "bingo.session.node"
    const val PlayerActorType: String = "bingo.player"
    const val RoomSpotDiscovery: String = "bingo.rooms"
    const val RoomSpotNode: String = "bingo.room.node"
    const val RoomRouteChannel: String = "bingo.rooms.route"
    const val PlayerJoinedPacket: String = "PlayerJoinedNotify"
    const val GameStartedPacket: String = "BingoGameStartedNotify"
    const val NumberDrawnPacket: String = "BingoNumberDrawnNotify"
    const val StatePacket: String = "BingoStateNotify"
    const val GameEndedPacket: String = "BingoGameEndedNotify"
}
