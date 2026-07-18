// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// actor가 spot을 떠나 있는 동안 도착한 메시지는 큐잉되고, 다시 합류하면 순서대로 배달된다.
//   bindings/java/gradlew -p . :kotlin-samples:runActorQueueExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.spot.MeshNodeOptions
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch

fun main() {
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("actor-queue", null)).use { node ->
            node.setBind(SampleSupport.tcpEndpoint())
            node.addChannel("app")
            node.start()
            node.createSpot().use { spot ->
                node.createActor("single-player").use { actor ->
                    ctx.createStreamSocket().use { stream ->
                        val sessionService = SampleSupport.startSessionService(node, stream)
                        ReadyBatch.create(16).use { ready ->
                            ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                val payloads = ArrayList<String>()

                                // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                                val session = RoutingId.from("single-player-session")
                                SampleSupport.bindSessionActor(node, sessionService, session, actor.ref())

                                val epoch = SampleSupport.joinLocalSpot(node, actor, spot, "join-first", "accepted", null)
                                SampleSupport.relaySessionMessage(sessionService, session, actor.ref(), "before")
                                SampleSupport.leaveLocalSpot(node, actor, epoch)
                                SampleSupport.relaySessionMessage(sessionService, session, actor.ref(), "between")
                                SampleSupport.joinLocalSpot(node, actor, spot, "join-second", "accepted", null)

                                SampleSupport.waitUntil("queued payloads") {
                                    SampleSupport.collectActorMessages(node, ready, recv, payloads)
                                    payloads.size >= 2
                                }
                                if (payloads != listOf("before", "between")) {
                                    throw IllegalStateException("queued payloads were not preserved: $payloads")
                                }

                                println("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"")
                            }
                        }
                        sessionService.close()
                    }
                }
            }
        }
    }
}
