package systems.zlink.samples.kotlin.bingo.server.api.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.spots.SpotHandleResolver
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

@ZLinkHandlerGroup(SampleNames.ApiChannel)
class MatchBingoHandler(
    private val routes: ZLinkRouteClient,
    private val spots: SpotHandleResolver,
) : ZLinkSuspendingRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
    override suspend fun handle(
        request: MatchBingoApiReq,
        context: ZLinkRequestContext,
    ) = run {
        val preferredOwner = RoutingId.from(request.actorNodeRid)
        val spot = spots.resolveSpotHandle(SampleNames.Mesh, preferredOwner)
            .await()
            .orElseThrow { IllegalStateException("spot not found: $preferredOwner") }
        val allocated = routes
            .requestToSpot(
                spot,
                AllocateBingoRoomReq(
                    request.actorId,
                    request.mode,
                    request.actorNodeRid,
                ),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(AllocateBingoRoomRes::class.java)
            .await()

        MatchBingoApiRes(allocated.roomId, allocated.roomOwnerNodeRid)
    }
}
