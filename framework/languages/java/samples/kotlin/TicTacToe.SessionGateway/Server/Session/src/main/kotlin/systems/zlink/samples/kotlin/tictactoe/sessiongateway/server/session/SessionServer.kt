package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.contracts.core.RoutingId
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSession
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.AuthenticateSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.CreateMatchSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.JoinMatchSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers.PlaceMarkSessionPacketHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object SessionServer {
    fun configureRelayNode(options: ZLinkFrameworkOptions) {
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel) { route ->
            route.bind(SampleTopology.SessionRouteEndpoint)
            route.configureRouting { routing ->
                routing.setRoutingId(RoutingId.from(SampleNames.SessionRid))
            }
            route.useManualConnections { endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint)
            }
        }
        options.addSpotMesh(SampleNames.SpotMesh) { mesh ->
            mesh.addNode(SampleNames.SessionRelayNode) { node ->
                node.enableRouter { router ->
                    router.setRouterBind(SampleTopology.SessionRouterEndpoint)
                    router.setRoutingId(RoutingId.from(SampleNames.SessionRid))
                }
                node.acceptSpotRoutesFromChannel(SampleNames.PlayRouteChannel)
                node.addSpotFactory(SessionRelaySpot::class.java)
            }
        }
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
    }

    fun configure(options: ZLinkFrameworkOptions) {
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
            stream.bind(SampleTopology.SessionEndpoint)
            stream.attachActorGateway(SampleNames.SessionRelayNode)
            stream.registerSession(PlayerSession::class.java)
            stream.addSessionPacketHandler(AuthenticateSessionPacketHandler::class.java)
            stream.addSessionPacketHandler(CreateMatchSessionPacketHandler::class.java)
            stream.addSessionPacketHandler(JoinMatchSessionPacketHandler::class.java)
            stream.addSessionPacketHandler(PlaceMarkSessionPacketHandler::class.java)
        }
    }
}
