// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotRpcExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.Received
import systems.zlink.contracts.service.spot.SpotNode
import systems.zlink.contracts.sockets.RecvFlags
import systems.zlink.contracts.service.spot.SpotDispatchEvent
import java.time.Duration

private fun waitPeer(node: SpotNode) {
    val deadline = System.nanoTime() + Duration.ofSeconds(15).toNanos()
    while (System.nanoTime() < deadline) {
        if (node.status().connectedPeerCount() > 0) return
        Thread.sleep(10)
    }
    throw IllegalStateException("spot peer not connected")
}

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { serverNode ->
            ctx.createSpotNode().use { clientNode ->
                serverNode.createSpot().use { server ->
                    clientNode.createSpot().use { client ->
                        serverNode.setRoutingId(RoutingId.from("rpc-server-node"))
                        clientNode.setRoutingId(RoutingId.from("rpc-client-node"))
                        server.setRoutingId(RoutingId.from("rpc-server-spot"))
                        client.setRoutingId(RoutingId.from("rpc-client-spot"))
                        // 라우티드 평면은 ROUTER bind가 필요하다 (pub bind보다 먼저).
                        serverNode.setRouterBind(SampleSupport.tcpEndpoint())
                        clientNode.setRouterBind(SampleSupport.tcpEndpoint())
                        val serverEndpoint = SampleSupport.tcpEndpoint()
                        val clientEndpoint = SampleSupport.tcpEndpoint()
                        serverNode.setPubBind(serverEndpoint)
                        clientNode.setPubBind(clientEndpoint)
                        serverNode.connectPeer(clientEndpoint)
                        clientNode.connectPeer(serverEndpoint)

                        // 서버 Spot은 라우티드 요청을 받아 같은 평면으로 응답한다.
                        server.setDispatchHandler { info ->
                            if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                                return@setDispatchHandler
                            }
                            Received().use { received ->
                                while (true) {
                                    val got = try {
                                        server.recvRouted(received, RecvFlags.DONT_WAIT)
                                    } catch (noData: RuntimeException) {
                                        break
                                    }
                                    if (!got) break
                                    Message.from("pong").use { reply ->
                                        received.reply().message(reply).submit()
                                    }
                                }
                            }
                        }

                        waitPeer(serverNode)
                        waitPeer(clientNode)

                        // 클라이언트 Spot이 서버 Spot으로 요청한다.
                        val reply = client.requestToSpot(
                            RoutingId.from("rpc-server-node"), RoutingId.from("rpc-server-spot")
                        ).message(Message.from("ping")).timeout(Duration.ofSeconds(3)).submit().toCompletableFuture().join()
                        println("[spot/rpc] request \"ping\" -> reply \"${reply[0].toUtf8String()}\"")
                        reply.forEach(Message::close)
                    }
                }
            }
        }
    }
// --8<-- [end:doc]
}
