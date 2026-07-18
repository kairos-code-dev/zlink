// 자립형 가이드 예제: 채널 요청/응답을 async로 대기.
// (10.0.0에서 route bridge는 제거됐고, 채널 요청은 MeshNode 위에서 pull dispatch로
//  완료를 회수한다. 여기선 그 완료 회수를 CompletableFuture로 감싸 async 형태를 보인다.)
//   bindings/java/gradlew -p . :kotlin-samples:runSpotRequestAsyncSample --no-daemon
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
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

fun main() {
    SampleSupport.ensureNative()
    val channelName = "orders"
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("orders-mesh", null)).use { requesterNode ->
            ctx.createMeshNode(MeshNodeOptions("orders-mesh", null)).use { responderNode ->
                val requesterEndpoint = SampleSupport.tcpEndpoint()
                val responderEndpoint = SampleSupport.tcpEndpoint()
                requesterNode.addChannel(channelName)
                responderNode.addChannel(channelName)
                requesterNode.setBind(requesterEndpoint)
                responderNode.setBind(responderEndpoint)
                requesterNode.start()
                responderNode.start()
                requesterNode.connectPeer(responderEndpoint)
                responderNode.connectPeer(requesterEndpoint)
                SampleSupport.waitMeshPeerConnected(requesterNode)
                SampleSupport.waitMeshPeerConnected(responderNode)

                val stop = AtomicBoolean(false)
                // 응답자: 채널 요청 레코드를 받아 "spot-pong"으로 응답한다.
                val responder = Thread({
                    ReadyBatch.create(16).use { ready ->
                        ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                            while (!stop.get()) {
                                SampleSupport.pumpReady(responderNode, ready, recv) { record, batch, index ->
                                    if (record.kind() == RecordKind.CHANNEL_REQUEST) {
                                        val request = batch.retainMessage(index)
                                        Message.from("spot-pong").use { reply ->
                                            Dispatch.reply(record.replyToken(), listOf(reply), SendFlags.NONE)
                                        }
                                        SampleSupport.closeAll(request)
                                    }
                                }
                                Thread.sleep(10)
                            }
                        }
                    }
                }, "spot-request-async-responder")
                responder.isDaemon = true
                responder.start()

                // 요청자: 채널로 요청을 제출하고 완료 레코드를 async로 기다린다.
                val completion = CompletableFuture<String>()
                ReadyBatch.create(16).use { ready ->
                    ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                        Message.from("spot-ping").use { request ->
                            requesterNode.requestToChannel(channelName, listOf(request),
                                SendFlags.NONE, Duration.ofSeconds(3))
                        }
                        SampleSupport.waitUntil("channel request completion") {
                            SampleSupport.pumpReady(requesterNode, ready, recv) { record, batch, index ->
                                if (record.kind() == RecordKind.COMPLETION &&
                                    record.operationKind() == OperationKind.CHANNEL_REQUEST &&
                                    record.terminalResult() == 0 && record.partCount() > 0) {
                                    val parts = batch.retainMessage(index)
                                    completion.complete(parts[0].toUtf8String())
                                    SampleSupport.closeAll(parts)
                                }
                            }
                            completion.isDone
                        }
                    }
                }
                stop.set(true)

                val reply = completion.get(5, TimeUnit.SECONDS)
                if (reply != "spot-pong") {
                    throw IllegalStateException("unexpected reply: $reply")
                }
                println("[spot/request/async] request: \"spot-ping\" -> reply: \"$reply\"")
            }
        }
    }
}
