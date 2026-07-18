// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 완료는 pull dispatch로 회수한다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotRpcExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.service.spot.Dispatch
import systems.zlink.contracts.service.spot.MeshNodeOptions
import systems.zlink.contracts.service.spot.OperationKind
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch
import systems.zlink.contracts.service.spot.RecordKind
import systems.zlink.contracts.sockets.SendFlags
import java.time.Duration

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("spot-rpc", null)).use { serverNode ->
            ctx.createMeshNode(MeshNodeOptions("spot-rpc", null)).use { clientNode ->
                val serverEndpoint = SampleSupport.tcpEndpoint()
                val clientEndpoint = SampleSupport.tcpEndpoint()
                serverNode.setBind(serverEndpoint)
                clientNode.setBind(clientEndpoint)
                serverNode.addChannel("app")
                clientNode.addChannel("app")
                serverNode.start()
                clientNode.start()
                serverNode.connectPeer(clientEndpoint)
                clientNode.connectPeer(serverEndpoint)

                serverNode.createSpot().use { server ->
                    clientNode.createSpot().use { client ->
                        ReadyBatch.create(16).use { ready ->
                            ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                SampleSupport.waitMeshPeerConnected(serverNode)
                                SampleSupport.waitMeshPeerConnected(clientNode)

                                // 클라이언트 Spot이 서버 Spot으로 요청을 제출한다.
                                Message.from("ping").use { ping ->
                                    client.requestToSpot(
                                        serverNode.status().routingId(),
                                        server.routingId(),
                                        server.status().lifecycleGeneration(),
                                        listOf(ping), SendFlags.NONE,
                                        Duration.ofSeconds(3))
                                }

                                val reply = arrayOfNulls<String>(1)
                                SampleSupport.waitUntil("spot rpc reply") {
                                    // 서버 Spot은 요청 레코드를 받아 같은 평면으로 응답한다.
                                    SampleSupport.pumpReady(serverNode, ready, recv) { record, batch, index ->
                                        if (record.kind() == RecordKind.SPOT_REQUEST) {
                                            val request = batch.retainMessage(index)
                                            Message.from("pong").use { pong ->
                                                Dispatch.reply(record.replyToken(), listOf(pong), SendFlags.NONE)
                                            }
                                            SampleSupport.closeAll(request)
                                        }
                                    }
                                    // 클라이언트는 완료 레코드에서 응답을 회수한다.
                                    SampleSupport.pumpReady(clientNode, ready, recv) { record, batch, index ->
                                        if (record.kind() == RecordKind.COMPLETION &&
                                            record.operationKind() == OperationKind.SPOT_REQUEST &&
                                            record.terminalResult() == 0 && record.partCount() > 0) {
                                            val parts = batch.retainMessage(index)
                                            reply[0] = parts[0].toUtf8String()
                                            SampleSupport.closeAll(parts)
                                        }
                                    }
                                    reply[0] != null
                                }

                                println("[spot/rpc] request \"ping\" -> reply \"${reply[0]}\"")
                            }
                        }
                    }
                }
            }
        }
    }
// --8<-- [end:doc]
}
