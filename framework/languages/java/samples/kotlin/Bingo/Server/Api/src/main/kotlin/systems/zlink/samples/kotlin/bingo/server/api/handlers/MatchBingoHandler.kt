package systems.zlink.samples.kotlin.bingo.server.api.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.ZLinkMessageContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

@ZLinkHandlerGroup(SampleNames.ApiChannel)
class MatchBingoHandler(
    private val routes: ZLinkRouteClient,
) : ZLinkSuspendingRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
    override suspend fun handle(
        request: MatchBingoApiReq,
        context: ZLinkMessageContext,
    ) = run {
        val allocated = routes
            .requestToChannel(
                SampleNames.RoomSpotDiscovery,
                AllocateBingoRoomReq(
                    request.actorId,
                    request.mode,
                ),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(AllocateBingoRoomRes::class.java)
            .await()

        MatchBingoApiRes(allocated.roomId)
    }
}
