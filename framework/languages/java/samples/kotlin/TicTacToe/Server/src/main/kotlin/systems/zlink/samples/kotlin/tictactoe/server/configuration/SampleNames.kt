package systems.zlink.samples.kotlin.tictactoe.server.configuration

import java.time.Duration

object SampleNames {
    const val ApiChannel: String = "tictactoe-api"
    const val PlayChannel: String = "tictactoe-play"
    const val SpotMesh: String = "tictactoe"
    const val PlayNode: String = "play"
    const val EntrySpotRoutingId: String = "tictactoe-entry"
    const val PlayStream: String = "play-stream"
    const val PlayActor: String = "play-actor"
    const val RouteChannel: String = "tictactoe-route"
    const val PlayerMilestoneTopic: String = "tictactoe.player.milestone"
    const val RequiredLevel: Int = 3
    val RequestTimeout: Duration = Duration.ofSeconds(5)
}
