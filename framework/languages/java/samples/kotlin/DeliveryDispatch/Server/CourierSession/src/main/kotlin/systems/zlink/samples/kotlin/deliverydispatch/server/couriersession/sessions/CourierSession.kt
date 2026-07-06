package systems.zlink.samples.kotlin.deliverydispatch.server.couriersession.sessions

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.locations.ZLinkSpotAddress
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ActorRefWire
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CourierDecisionMsg
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorRes

class CourierSession(
    private val sessionContext: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
    private val routes: ZLinkRouteClient,
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
        val actorRef = findOrEnsureActor(request.courierId)
        val actor = sessionContext.actors().find(actorRef.actorId)
            .orElseGet {
                ZLinkAwait.await(
                    sessionContext.actors().bind(actorRef.toActorRef()),
                )
            }
        ZLinkAwait.await(
            actor.relay(
                ZLinkMessage.of(
                    BindCourierSessionReq(
                        courierId = request.courierId,
                        actor = actorRef,
                        sessionRoute = sessionContext.sessionId(),
                    ),
                ),
            ),
        )
        sessionContext.client()
            .reply(BindCourierSessionRes(request.courierId, actorRef.nodeRid, sessionContext.sessionId()))
            .submit()
    }

    private fun findOrEnsureActor(courierId: String): ActorRefWire {
        val nodeRid = RoutingId.from(SampleTopology.courierPlacement(courierId))
        val address = ZLinkSpotAddress(SampleNames.CourierSpotMesh, nodeRid, nodeRid)
        val found = routes
            .requestToSpot(SampleNames.CourierSpotMesh, address, FindCourierActorReq(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .await(FindCourierActorRes::class.java)
        if (found.actor != null) {
            return found.actor
        }
        return routes
            .requestToSpot(SampleNames.CourierSpotMesh, address, EnsureCourierActorReq(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .await(EnsureCourierActorRes::class.java)
            .actor
    }
}
