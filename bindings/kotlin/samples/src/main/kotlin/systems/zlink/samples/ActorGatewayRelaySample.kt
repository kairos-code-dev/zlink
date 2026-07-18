// SPDX-License-Identifier: MPL-2.0
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch
import systems.zlink.contracts.service.spot.MeshNodeOptions
import java.util.concurrent.CountDownLatch

fun main() {
    SampleSupport.ensureNative()
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("actor-gateway-relay", null)).use { node ->
            node.setBind(SampleSupport.tcpEndpoint())
            node.addChannel("app")
            node.start()
            node.createSpot().use { spot ->
                node.createActor("play-session-actor").use { actor ->
                    ctx.createStreamSocket().use { stream ->
                        ReadyBatch.create(16).use { ready ->
                            ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                val payloads = ArrayList<String>()
                                val sessionRid = arrayOfNulls<RoutingId>(1)
                                val sessionReady = CountDownLatch(1)
                                // 원격 클라이언트가 접속하면 게이트웨이가 session routing id를 알려준다.
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
                                        SampleSupport.sendStreamPacket(client, "hello-gateway".toByteArray(Charsets.UTF_8))
                                        SampleSupport.await(sessionReady, "stream session")
                                        val session = sessionRid[0]!!

                                        SampleSupport.bindSessionActor(node, sessionService, session, actor.ref())
                                        // actor가 play spot에 합류한다 (호스트가 admit).
                                        val epoch = SampleSupport.joinLocalSpot(node, actor, spot, "join-play", "accepted", null)

                                        // 게이트웨이가 클라이언트 입력을 바인딩된 actor로 relay한다.
                                        SampleSupport.relaySessionMessage(sessionService, session, actor.ref(), "client-input")
                                        SampleSupport.waitUntil("actor relay") {
                                            SampleSupport.collectActorMessages(node, ready, recv, payloads)
                                            payloads.contains("client-input")
                                        }

                                        println("[actor/gateway] stream payload: \"client-input\" -> actor: \"client-input\"")
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
