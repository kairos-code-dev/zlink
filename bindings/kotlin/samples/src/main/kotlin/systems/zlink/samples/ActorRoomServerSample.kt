// SPDX-License-Identifier: MPL-2.0
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.spot.MeshNodeOptions
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch

fun main() {
    SampleSupport.ensureNative()
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("actor-room-server", null)).use { node ->
            node.setBind(SampleSupport.tcpEndpoint())
            node.addChannel("app")
            node.start()
            node.createSpot().use { spot ->
                node.createActor("room-player-1").use { actor ->
                    ctx.createStreamSocket().use { stream ->
                        val sessionService = SampleSupport.startSessionService(node, stream)
                        ReadyBatch.create(16).use { ready ->
                            ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                val payloads = ArrayList<String>()

                                // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                                val sessionRid = RoutingId.from("room-session".toByteArray(Charsets.UTF_8))
                                SampleSupport.bindSessionActor(node, sessionService, sessionRid, actor.ref())

                                // actor가 spot에 합류한다 — 호스트가 join 요청을 받아 admit한다.
                                val epoch = SampleSupport.joinLocalSpot(node, actor, spot, "enter-room", "accepted") { msg ->
                                    if (msg != "enter-room") {
                                        throw IllegalStateException("unexpected join message: $msg")
                                    }
                                }

                                // 바인딩된 STREAM 세션으로 actor에게 메시지를 relay한다.
                                SampleSupport.relaySessionMessage(sessionService, sessionRid, actor.ref(), "move:north")
                                SampleSupport.waitUntil("actor payload") {
                                    SampleSupport.collectActorMessages(node, ready, recv, payloads)
                                    payloads.contains("move:north")
                                }

                                println("[actor/room] stream payload: \"move:north\" -> actor: \"move:north\"")
                                SampleSupport.leaveLocalSpot(node, actor, epoch)
                            }
                        }
                        sessionService.close()
                    }
                }
            }
        }
    }
}
