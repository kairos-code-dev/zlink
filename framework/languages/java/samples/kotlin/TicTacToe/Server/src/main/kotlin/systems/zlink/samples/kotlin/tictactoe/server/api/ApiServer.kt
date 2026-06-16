package systems.zlink.samples.kotlin.tictactoe.server.api

import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings

object ApiServer {
    fun configure(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            SampleLogging.configure(settings, "api")
            options.codecs().addMessagePack()
            options.addHandlersFromPackageOf(ApiServer::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(settings.apiChannelEndpoint)
                .addHandlerGroup("api")
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableClient(settings.playChannelEndpoint)
        }
}
