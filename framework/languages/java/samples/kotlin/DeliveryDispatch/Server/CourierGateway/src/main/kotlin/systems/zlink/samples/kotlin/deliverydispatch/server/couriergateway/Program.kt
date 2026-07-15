package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway

import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology

fun main(args: Array<String>) {
    SampleTopology.configure(args)
    CourierGatewayApplication.run()
}
