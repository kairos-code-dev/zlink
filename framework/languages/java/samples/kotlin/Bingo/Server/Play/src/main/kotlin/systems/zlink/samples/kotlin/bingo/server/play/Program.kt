package systems.zlink.samples.kotlin.bingo.server.play

import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology

fun main(args: Array<String>) {
    SampleTopology.configure(args)
    PlayServerApplication.run()
}
