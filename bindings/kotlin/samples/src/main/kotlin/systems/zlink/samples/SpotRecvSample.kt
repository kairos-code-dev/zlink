// 자립형 가이드 예제: Spot ↔ Spot 토픽 발행/구독을 pull dispatch로 받는다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotRecvSample --no-daemon
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
    SampleSupport.ensureNative()
    val serviceName = "direct"
    val channel = "room"
    val topic = "room:lobby"
    val payload = "hello-spot"
    Zlink.createContext().use { publisherContext ->
        Zlink.createContext().use { subscriberContext ->
            publisherContext.createMeshNode(MeshNodeOptions("spot-recv", null)).use { publisherNode ->
                subscriberContext.createMeshNode(MeshNodeOptions("spot-recv", null)).use { subscriberNode ->
                    val publisherEndpoint = SampleSupport.tcpEndpoint()
                    val subscriberEndpoint = SampleSupport.tcpEndpoint()
                    publisherNode.addChannel(channel)
                    subscriberNode.addChannel(channel)
                    publisherNode.setBind(publisherEndpoint)
                    subscriberNode.setBind(subscriberEndpoint)
                    publisherNode.start()
                    subscriberNode.start()
                    publisherNode.connectPeer(subscriberEndpoint)
                    subscriberNode.connectPeer(publisherEndpoint)

                    publisherNode.createSpot().use { publisher ->
                        subscriberNode.createSpot().use { subscriber ->
                            ReadyBatch.create(16).use { ready ->
                                ReceiveBatch.create(64, 256, 1 shl 16).use { recv ->
                                    subscriber.setSubscription(channel, topic, SubscriptionKind.EXACT)
                                    SampleSupport.waitMeshPeerConnected(publisherNode)
                                    SampleSupport.waitMeshPeerConnected(subscriberNode)

                                    val received = arrayOfNulls<String>(2)
                                    SampleSupport.waitUntil("spot recv sample") {
                                        Message.from(payload).use { message ->
                                            publisher.publish(channel, topic, listOf(message), SendFlags.NONE)
                                        }
                                        SampleSupport.pumpReady(subscriberNode, ready, recv) { record, batch, index ->
                                            if (record.kind() == RecordKind.SPOT_MULTICAST) {
                                                val parts = batch.retainMessage(index)
                                                received[0] = record.topic()
                                                received[1] = parts[0].toUtf8String()
                                                SampleSupport.closeAll(parts)
                                            }
                                        }
                                        received[1] != null
                                    }

                                    println("[spot/recv] service: \"$serviceName\" tick: 1 publish: " +
                                        "\"$topic/$payload\" -> recv: \"${received[0]}/${received[1]}\"")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
