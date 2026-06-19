package systems.zlink.samples.kotlin.tictactoe.server.configuration

import java.time.Duration

object SampleNames {
    const val ApiChannel: String = "tictactoe-api"
    const val PlayChannel: String = "tictactoe-play"
    const val SpotMesh: String = "tictactoe"
    const val PlayNode: String = "play"
    const val PlayNodeRoutingId: String = "3200"
    const val EntrySpotRoutingId: String = "3201"
    const val PlayStream: String = "play-stream"
    const val PlayActor: String = "play-actor"
    val RequestTimeout: Duration = Duration.ofSeconds(5)
}
