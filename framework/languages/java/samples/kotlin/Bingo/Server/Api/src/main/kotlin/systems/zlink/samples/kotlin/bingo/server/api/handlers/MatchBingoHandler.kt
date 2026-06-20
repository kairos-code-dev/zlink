package systems.zlink.samples.kotlin.bingo.server.api.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

@ZLinkHandlerGroup("api")
class MatchBingoHandler(
    private val routes: ZLinkRouteClient,
) : ZLinkSuspendingRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
    override suspend fun handle(
        request: MatchBingoApiReq,
        context: ZLinkRequestContext,
    ) = run {
        val allocated = routes
            .requestTo(
                SampleNames.PlayChannel,
                RoutingId.from(request.actorNodeRid),
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
