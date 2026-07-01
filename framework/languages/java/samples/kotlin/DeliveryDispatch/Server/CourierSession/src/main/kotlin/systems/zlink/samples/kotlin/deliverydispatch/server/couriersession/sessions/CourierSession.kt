package systems.zlink.samples.kotlin.deliverydispatch.server.couriersession.sessions

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.contracts.core.RoutingId
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CourierDecisionMsg

class CourierSession(
    private val sessionContext: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
    private val channels: ZLinkClient,
) : ZLinkSession {
    override fun context(): ZLinkSessionContext = sessionContext

    override fun onConnected() {
    }

    override fun onDisconnected() {
        sessionContext.actors().bound()
            .forEach { actor -> ZLinkAwait.await(actor.notifyDisconnected()) }
    }

    override fun onError(error: ZLinkStreamError) {
    }

    override fun onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        if (dispatch.packetName() == "BindCourierSessionReq") {
            handleBindCourierSessionReq(payload)
            return
        }
        val handled = ZLinkAwait.await(handlers.tryHandleAsync(sessionContext, dispatch, payload))
        if (handled) {
            return
        }
        val decision = payload.decode(CourierDecisionMsg::class.java)
        val actor = sessionContext.actors().find(decision.courierId)
            .orElseThrow { IllegalStateException("Courier actor is not bound: ${decision.courierId}") }
        ZLinkAwait.await(actor.relay(payload))
    }

    private fun handleBindCourierSessionReq(payload: ZLinkMessage) {
        val request = payload.decode(BindCourierSessionReq::class.java)
        val bound = channels
            .requestToChannel(SampleNames.CourierChannel, BindCourierReq(request.courierId, sessionContext.sessionId()))
            .await(BindCourierRes::class.java)
        val actor = sessionContext.actors().find(bound.actor.actorId)
            .orElseGet {
                ZLinkAwait.await(
                    sessionContext.actors().bind(
                        ZLinkActorRef(
                            RoutingId.from(bound.actor.nodeRid),
                            bound.actor.actorId,
                            bound.actor.generation,
                        ),
                    ),
                )
            }
        ZLinkAwait.await(
            actor.relay(
                ZLinkMessage.of(
                    BindCourierSessionReq(
                        courierId = bound.courierId,
                        actor = bound.actor,
                        sessionRoute = bound.sessionRoute,
                    ),
                ),
            ),
        )
        sessionContext.client()
            .reply(BindCourierSessionRes(bound.courierId, bound.actor, bound.sessionRoute))
            .submit()
    }
}
