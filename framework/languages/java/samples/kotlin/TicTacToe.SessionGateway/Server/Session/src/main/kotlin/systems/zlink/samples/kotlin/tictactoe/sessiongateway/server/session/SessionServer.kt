package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.contracts.core.RoutingId
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSession
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleSessionNode
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object SessionServer {
    fun configureRelayNode(options: ZLinkFrameworkOptions, sessionNode: SampleSessionNode) {
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel) { route ->
            route.bind(sessionNode.routeEndpoint)
            route.configureRouting { routing ->
                routing.setRoutingId(RoutingId.from(sessionNode.routingId))
            }
            route.useManualConnections { endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint)
            }
        }
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.SessionRelayNode) { node ->
                node.enableRouter { router ->
                    router.bindRouter(sessionNode.routerEndpoint)
                    router.setRoutingId(RoutingId.from(sessionNode.routingId))
                }
                node.acceptSpotRoutesFromChannel(SampleNames.PlayRouteChannel)
                node.addSpotFactory(SessionRelaySpot::class.java)
            }
        }
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
    }

    fun configure(options: ZLinkFrameworkOptions, sessionNode: SampleSessionNode) {
        options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints ->
                    endpoints.connect(SampleTopology.ApiEndpoint)
                }
            }
        }
        options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints ->
                    endpoints.connect(SampleTopology.PlayChannelEndpoint)
                }
            }
        }
        options.addStreamNode(SampleNames.GatewayStream) { stream ->
            stream.bind(sessionNode.streamEndpoint)
            stream.attachActorGateway(SampleNames.SessionRelayNode)
            stream.registerSession(PlayerSession::class.java)
        }
    }
}
