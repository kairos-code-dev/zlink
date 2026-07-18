// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로 옮겨 간다.
//   bindings/java/gradlew -p . :kotlin-samples:runActorSequentialExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.spot.MeshNodeOptions
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("actor-sequential", null)).use { node ->
            node.setBind(SampleSupport.tcpEndpoint())
            node.addChannel("app")
            node.start()
            node.createSpot().use { room ->
                // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
                node.createActor("player").use { player ->
                    ctx.createStreamSocket().use { stream ->
                        val sessionService = SampleSupport.startSessionService(node, stream)
                        ReadyBatch.create(16).use { ready ->
                            ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                val processed = ArrayList<String>()

                                // STREAM session에 actor를 bind한다.
                                val session = RoutingId.from("player-session")
                                SampleSupport.bindSessionActor(node, sessionService, session, player.ref())

                                // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
                                val epoch = SampleSupport.joinLocalSpot(node, player, room, "enter-room", "accepted", null)

                                // STREAM이 플레이어 입력을 연달아 relay한다 — 순서대로 처리된다.
                                val commands = listOf("move", "attack", "loot")
                                for (command in commands) {
                                    SampleSupport.relaySessionMessage(sessionService, session, player.ref(), command)
                                }

                                SampleSupport.waitUntil("sequential actor messages") {
                                    SampleSupport.collectActorMessages(node, ready, recv, processed)
                                    processed.size >= commands.size
                                }
                                if (processed != commands) {
                                    throw IllegalStateException("messages were not processed in order: $processed")
                                }

                                SampleSupport.leaveLocalSpot(node, player, epoch)
                                println("[actor/sequential] processed in order: move -> attack -> loot")
                            }
                        }
                        sessionService.close()
                    }
                }
            }
        }
    }
// --8<-- [end:doc]
}
