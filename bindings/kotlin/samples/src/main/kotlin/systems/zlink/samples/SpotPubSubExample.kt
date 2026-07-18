// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 채널 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotPubSubExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.service.spot.MeshNodeOptions
import systems.zlink.contracts.service.spot.ReadyBatch
import systems.zlink.contracts.service.spot.ReceiveBatch
import systems.zlink.contracts.service.spot.RecordKind
import systems.zlink.contracts.service.spot.SubscriptionKind
import systems.zlink.contracts.sockets.SendFlags

fun main() {
// --8<-- [start:doc]
    val channel = "room"
    val topic = "room:lobby"
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("spot-pubsub", null)).use { publisherNode ->
            ctx.createMeshNode(MeshNodeOptions("spot-pubsub", null)).use { subscriberNode ->
                val pubEndpoint = SampleSupport.tcpEndpoint()
                val subEndpoint = SampleSupport.tcpEndpoint()
                publisherNode.addChannel(channel)
                subscriberNode.addChannel(channel)
                publisherNode.setBind(pubEndpoint)
                subscriberNode.setBind(subEndpoint)
                publisherNode.start()
                subscriberNode.start()
                publisherNode.connectPeer(subEndpoint)
                subscriberNode.connectPeer(pubEndpoint)

                publisherNode.createSpot().use { publisher ->
                    subscriberNode.createSpot().use { subscriber ->
                        ReadyBatch.create(16).use { ready ->
                            ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                // 구독자는 받을 채널 토픽을 등록한다.
                                subscriber.setSubscription(channel, topic, SubscriptionKind.EXACT)
                                SampleSupport.waitMeshPeerConnected(publisherNode)
                                SampleSupport.waitMeshPeerConnected(subscriberNode)

                                val got = arrayOfNulls<String>(2)
                                SampleSupport.waitUntil("spot delivery") {
                                    Message.from("hello-everyone").use { m ->
                                        publisher.publish(channel, topic, listOf(m), SendFlags.NONE)
                                    }
                                    SampleSupport.pumpReady(subscriberNode, ready, recv) { record, batch, index ->
                                        if (record.kind() == RecordKind.SPOT_MULTICAST) {
                                            val parts = batch.retainMessage(index)
                                            got[0] = record.topic()
                                            got[1] = parts[0].toUtf8String()
                                            SampleSupport.closeAll(parts)
                                        }
                                    }
                                    got[1] != null
                                }

                                println("[spot/pubsub] topic \"${got[0]}\" -> recv: \"${got[1]}\"")
                            }
                        }
                    }
                }
            }
        }
    }
// --8<-- [end:doc]
}
