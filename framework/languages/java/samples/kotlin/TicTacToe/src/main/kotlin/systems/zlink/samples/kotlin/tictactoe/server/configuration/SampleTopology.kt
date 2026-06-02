package systems.zlink.samples.kotlin.tictactoe.server.configuration

object SampleTopology {
    const val ApiEndpoint: String = "inproc://zlink-kotlin-sample-tictactoe-api"
    const val PlayStreamEndpoint: String = "inproc://zlink-kotlin-sample-tictactoe-stream"
}
