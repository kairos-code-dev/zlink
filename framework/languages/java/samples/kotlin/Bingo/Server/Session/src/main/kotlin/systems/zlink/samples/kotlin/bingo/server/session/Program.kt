package systems.zlink.samples.kotlin.bingo.server.session

import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology

fun main(args: Array<String>) {
    SampleTopology.configure(args)
    SessionServerApplication.run()
}
