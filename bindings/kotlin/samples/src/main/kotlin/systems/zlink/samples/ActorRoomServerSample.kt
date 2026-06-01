package systems.zlink.samples

import systems.zlink.contracts.core.Context
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.eventing.MonitorEventType
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.Received
import systems.zlink.contracts.service.spot.Actor
import systems.zlink.contracts.service.spot.Spot
import systems.zlink.contracts.service.spot.SpotNode
import systems.zlink.contracts.sockets.RecvFlags
import systems.zlink.contracts.sockets.RequestResult
import systems.zlink.contracts.sockets.SpotDispatchEvent
import systems.zlink.contracts.sockets.StreamSocket
import java.time.Duration

fun main() {
    SampleSupport.ensureNative()
    val endpoint = SampleSupport.tcpEndpoint()

    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { node ->
            node.createSpot().use { spot ->
                ctx.createStreamSocket().use { stream ->
                    stream.monitorOpen(MonitorEventType.ACCEPTED).use { monitor ->
                        val actor = node.createActor("room-player-1")
                        val actorRef = actor.ref()
                        val joins = mutableListOf<String>()
                        val replies = mutableListOf<RequestResult>()
                        val payloads = mutableListOf<String>()

                        spot.setDispatchHandler { info ->
                            when (info.event()) {
                                SpotDispatchEvent.ACTOR_JOIN_READABLE ->
                                    spot.recvActorJoin(RecvFlags.DONT_WAIT)?.use { request ->
                                        joins.add(request.message().toUtf8String())
                                        Message.from("accepted").use { reply ->
                                            spot.replyActorJoin(request, 0).message(reply).submit()
                                        }
                                    }
                                SpotDispatchEvent.ACTOR_READABLE ->
                                    for (part in info.actorMessages()) {
                                        part.use { payloads.add(it.message().toUtf8String()) }
                                    }
                                else -> {}
                            }
                        }

                        stream.bind(endpoint)
                        stream.attachActorGateway(node)
                        SampleSupport.connectRawTcp(endpoint).use { client ->
                            SampleSupport.waitStreamConnected(monitor)
                            SampleSupport.sendRawTcp(client, "seed".toByteArray())
                            val sessionRid: RoutingId = Received().use { received ->
                                stream.recv(received, RecvFlags.NONE)
                                received.routingId.orElseThrow()
                            }
                            stream.bindActor(sessionRid, actorRef)
                                .timeout(Duration.ofSeconds(2))
                                .submitAsync().join().forEach(Message::close)

                            Message.from("enter-room").use { request ->
                                actor.join(spot).message(request).timeout(Duration.ofSeconds(2))
                                    .submit { result, messages ->
                                        replies.add(result.result())
                                        messages.forEach(Message::close)
                                    }
                            }
                            SampleSupport.waitUntil("actor join") { replies.isNotEmpty() }

                            check(joins == listOf("enter-room")
                                && replies[0] == RequestResult.OK
                                && spot.actors().isNotEmpty()) { "actor room join failed" }

                            Message.from("move:north").use { inbound ->
                                stream.sendBoundActor(sessionRid, "room-player-1")
                                    .message(inbound).submit()
                            }
                            SampleSupport.waitUntil("actor payload") { payloads.contains("move:north") }

                            actor.leave(spot).submitAsync().join().forEach(Message::close)
                            actor.close()
                        }
                        println("[actor/room] stream payload: \"move:north\" -> actor: \"move:north\"")
                    }
                }
            }
        }
    }
}
