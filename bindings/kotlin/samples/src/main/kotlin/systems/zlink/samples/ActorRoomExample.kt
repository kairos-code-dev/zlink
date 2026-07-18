// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
//   bindings/java/gradlew -p . :kotlin-samples:runActorRoomExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.spot.Actor
import systems.zlink.contracts.service.spot.MeshNodeOptions
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("actor-room", null)).use { node ->
            node.setBind(SampleSupport.tcpEndpoint())
            node.addChannel("app")
            node.start()
            node.createSpot().use { room ->
                node.createActor("player-1").use { player1 ->
                    node.createActor("player-2").use { player2 ->
                        ctx.createStreamSocket().use { stream ->
                            val sessionService = SampleSupport.startSessionService(node, stream)
                            ReadyBatch.create(16).use { ready ->
                                ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                    val received = ArrayList<String>()

                                    // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                                    val session = RoutingId.from("game-room-session")
                                    SampleSupport.bindSessionActor(node, sessionService, session, player1.ref())
                                    SampleSupport.bindSessionActor(node, sessionService, session, player2.ref())

                                    // 두 플레이어가 방에 합류한다 (호스트가 각각 admit).
                                    val epoch1 = SampleSupport.joinLocalSpot(node, player1, room, "enter-room", "accepted", null)
                                    val epoch2 = SampleSupport.joinLocalSpot(node, player2, room, "enter-room", "accepted", null)

                                    fun sendAndWait(player: Actor, text: String, want: Int) {
                                        SampleSupport.relaySessionMessage(sessionService, session, player.ref(), text)
                                        SampleSupport.waitUntil("message to ${player.ref().actorId()}") {
                                            SampleSupport.collectActorMessages(node, ready, recv, received)
                                            received.size >= want
                                        }
                                    }

                                    // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
                                    sendAndWait(player1, "your-turn", 1)
                                    sendAndWait(player2, "wait", 2)

                                    if (received != listOf("your-turn", "wait")) {
                                        throw IllegalStateException("messages were not routed per actor: $received")
                                    }

                                    SampleSupport.leaveLocalSpot(node, player1, epoch1)
                                    SampleSupport.leaveLocalSpot(node, player2, epoch2)
                                    println("[actor/room] player-1: \"your-turn\", player-2: \"wait\"")
                                }
                            }
                            sessionService.close()
                        }
                    }
                }
            }
        }
    }
// --8<-- [end:doc]
}
