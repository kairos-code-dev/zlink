package systems.zlink.samples.kotlin.bingo.server.api

import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology

fun main(args: Array<String>) {
    SampleTopology.configure(args)
    ApiServerApplication.run()
}
