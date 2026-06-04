package systems.zlink.samples.kotlin.tictactoe.server.play

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ApplicationContextInitializer
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActorFactory
import systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.sessions.PlaySession

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [PlayServer::class],
)
class PlayServer {
    @Bean
    fun playOptions(settings: SampleSettings): ZLinkFrameworkOptionsCustomizer =
        ZLinkFrameworkOptionsCustomizer { options ->
            SampleSettings.setCurrent(settings)
            SampleLogging.configure(settings, "play")
            options.codecs().addJson()
            options.addHandlersFromPackageOf(PlayServer::class.java)
            options.addActorFactory(SampleNames.PlayActor, PlayActorFactory::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
                channel.enableClient { client ->
                    client.useManualConnections { endpoints ->
                        endpoints.connect(settings.apiChannelEndpoint)
                    }
                }
            }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
                channel.enableServer { server -> server.bind(settings.playChannelEndpoint) }
                channel.addHandlerGroup(SampleNames.PlayChannel)
            }
            options.addRouteMeshChannel(SampleNames.PlayRouteChannel) { route ->
                route.bind(settings.playRouterEndpoint)
                route.configureRouting { routing ->
                    routing.setRoutingId(RoutingId.from(SampleNames.PlayRouterId))
                }
                route.useManualConnections { endpoints ->
                    endpoints.connect(settings.playRouterEndpoint)
                }
            }
            options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
                mesh.addNode(SampleNames.PlayNode) { node ->
                    node.enableRouter { router ->
                        router.setRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId))
                        router.setRouterBind(settings.spotEndpoint)
                    }
                    node.configureEntrySpot { entry ->
                        entry.setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
                    }
                    node.addEntrySpot(PlayEntrySpot::class.java)
                    node.addSpotFactory(TicTacToeGame::class.java)
                }
            }
            options.addStreamNode(SampleNames.PlayStream) { stream ->
                stream.bind(settings.playEndpoint)
                stream.attachActorGateway(SampleNames.PlayNode)
                stream.registerSession(PlaySession::class.java)
            }
        }

    companion object {
        fun start(settings: SampleSettings): ConfigurableApplicationContext =
            SpringApplicationBuilder(PlayServer::class.java).also { builder ->
                builder.application().setKeepAlive(true)
            }
                .web(WebApplicationType.NONE)
                .initializers(ApplicationContextInitializer<ConfigurableApplicationContext> { context ->
                    context.beanFactory.registerSingleton("sampleSettings", settings)
                })
                .run()
    }
}
