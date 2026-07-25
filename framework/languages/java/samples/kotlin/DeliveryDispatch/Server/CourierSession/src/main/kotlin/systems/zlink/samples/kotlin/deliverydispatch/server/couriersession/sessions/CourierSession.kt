package systems.zlink.samples.kotlin.deliverydispatch.server.couriersession.sessions

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingSession
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.spots.SpotHandleResolver
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionMessageContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.framework.actors.ActorRefSnapshot
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
    private val spots: SpotHandleResolver,
) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = sessionContext

    override suspend fun onConnectedSuspending() {
    }

    override suspend fun onDisconnectedSuspending() {
        for (actor in sessionContext.actors().bound()) actor.notifyDisconnected().await()
    }

    override suspend fun onErrorSuspending(error: ZLinkStreamError) {
    }

    override suspend fun onDispatchSuspending(dispatch: ZLinkSessionMessageContext, payload: ZLinkMessage) {
        if (dispatch.packetName() == "BindCourierSessionReq") {
            handleBindCourierSessionReq(dispatch, payload)
            return
        }
        val handled = handlers.tryHandle(sessionContext, dispatch, payload).await()
        if (handled) {
            return
        }
        val decision = payload.decode(CourierDecisionMsg::class.java)
        val actor = sessionContext.actors().find(decision.courierId)
            .orElseThrow { IllegalStateException("Courier actor is not bound: ${decision.courierId}") }
        actor.relay(dispatch, payload).await()
    }

    private suspend fun handleBindCourierSessionReq(dispatch: ZLinkSessionMessageContext, payload: ZLinkMessage) {
        val request = payload.decode(BindCourierSessionReq::class.java)
        val actorRef = findOrEnsureActor(request.courierId)
        val actor = sessionContext.actors().find(actorRef.actorId).orElse(null)
            ?: sessionContext.actors().bind(actorRef.toActorRef()).await()
        actor.relay(
                dispatch,
                ZLinkMessage.of(
                    BindCourierSessionReq(
                        courierId = request.courierId,
                        actor = actorRef,
                        sessionRoute = sessionContext.sessionId(),
                    ),
                ),
            ).await()
        sessionContext.client()
            .reply(BindCourierSessionRes(request.courierId, actorRef, sessionContext.sessionId()))
            .submit()
    }

    private suspend fun findOrEnsureActor(courierId: String): ActorRefSnapshot {
        val nodeRid = RoutingId.from(SampleTopology.courierPlacement(courierId))
        val spotRef = spots.resolveSpotHandle(nodeRid).await()
            .orElseThrow { IllegalStateException("spot not found: $nodeRid") }
        val found = routes
            .requestToSpot(spotRef, FindCourierActorReq(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .submit(FindCourierActorRes::class.java).await()
        val foundActor = found.actor
        if (foundActor != null) {
            return foundActor
        }
        return routes
            .requestToSpot(spotRef, EnsureCourierActorReq(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .submit(EnsureCourierActorRes::class.java).await()
            .actor
    }
}
