package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration

object SampleNames {
    const val ApiChannel: String = "tictactoe-session-api"
    const val PlayChannel: String = "tictactoe-session-play"
    const val SpotMesh: String = "tictactoe"
    const val PlayRouteChannel: String = "play-route"
    const val PlayNode: String = "play"
    const val SessionRelayNode: String = "session-relay"
    const val SessionRid: String = "1101"
    const val PlayRid: String = "2202"
    const val GatewayStream: String = "gateway"
    const val PlayerActorType: String = "player"
    const val EntrySpotRoutingId: String = "3301"
    const val XActorId: String = "player-x"
    const val OActorId: String = "player-o"
    const val TurnChangedPacket: String = "TurnChangedNotify"
    const val OpponentJoinedPacket: String = "OpponentJoinedNotify"
    const val GameEndedPacket: String = "GameEndedNotify"
}
