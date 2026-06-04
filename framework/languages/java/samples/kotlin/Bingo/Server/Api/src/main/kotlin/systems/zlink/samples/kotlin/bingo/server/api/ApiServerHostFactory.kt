package systems.zlink.samples.kotlin.bingo.server.api

import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

object ApiServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            options.addHandlersFromPackageOf(ApiServerHostFactory::class.java)
            options.useDiscovery { discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint) }
            options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
                channel.enableServer { server -> server.bind(SampleTopology.ApiChannelEndpoint) }
                channel.addHandlerGroup("api")
            }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel -> channel.enableClient() }
        }
}
