package systems.zlink.samples.kotlin.bingo.server.configuration

object SampleNames {
    const val ApiChannel: String = "bingo.api"
    const val PlayChannel: String = "bingo.play"
    const val StreamNode: String = "bingo.client.stream"
    const val SessionSpotNode: String = "bingo.session.node"
    const val PlayerActorType: String = "bingo.player"
    const val RoomSpotDiscovery: String = "bingo.rooms"
    const val RoomSpotNode: String = "bingo.room.node"
    const val RoomRouteChannel: String = "bingo.rooms.route"
    const val WinnerTopic: String = "bingo.room.reward"
}
