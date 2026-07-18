// SPDX-License-Identifier: MPL-2.0
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.spot.MeshNodeOptions
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch
import java.util.concurrent.CountDownLatch

fun main() {
    SampleSupport.ensureNative()
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("actor-single-player-queue", null)).use { node ->
            node.setBind(SampleSupport.tcpEndpoint())
            node.addChannel("app")
            node.start()
            node.createSpot().use { spot ->
                node.createActor("single-player").use { actor ->
                    ctx.createStreamSocket().use { stream ->
                        ReadyBatch.create(16).use { ready ->
                            ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                val actorMessages = ArrayList<String>()
                                val sessionRid = arrayOfNulls<RoutingId>(1)
                                val sessionReady = CountDownLatch(1)
                                stream.onPacket { routingId, header, body ->
                                    SampleSupport.closeQuietly(header)
                                    SampleSupport.closeQuietly(body)
                                    if (sessionRid[0] == null) {
                                        sessionRid[0] = routingId
                                        sessionReady.countDown()
                                    }
                                }
                                val endpoint = SampleSupport.tcpEndpoint()
                                stream.bind(endpoint)

                                // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                                val sessionService = SampleSupport.startSessionService(node, stream)
                                try {
                                    SampleSupport.connectRawTcp(endpoint).use { client ->
                                        SampleSupport.sendStreamPacket(client, "open".toByteArray(Charsets.UTF_8))
                                        SampleSupport.await(sessionReady, "stream session")
                                        val session = sessionRid[0]!!
                                        SampleSupport.bindSessionActor(node, sessionService, session, actor.ref())

                                        var epoch = SampleSupport.joinLocalSpot(node, actor, spot, "join-first", "accepted", null)
                                        SampleSupport.relaySessionMessage(sessionService, session, actor.ref(), "before")
                                        SampleSupport.waitUntil("first actor message") {
                                            SampleSupport.collectActorMessages(node, ready, recv, actorMessages)
                                            actorMessages.contains("before")
                                        }

                                        SampleSupport.leaveLocalSpot(node, actor, epoch)
                                        SampleSupport.relaySessionMessage(sessionService, session, actor.ref(), "between")
                                        epoch = SampleSupport.joinLocalSpot(node, actor, spot, "join-second", "accepted", null)
                                        SampleSupport.waitUntil("queued actor message") {
                                            SampleSupport.collectActorMessages(node, ready, recv, actorMessages)
                                            actorMessages.contains("between")
                                        }

                                        println("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"")
                                        SampleSupport.leaveLocalSpot(node, actor, epoch)
                                        SampleSupport.unbindSessionActor(node, sessionService, session, actor.ref())
                                    }
                                } finally {
                                    SampleSupport.closeQuietly(sessionService)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
