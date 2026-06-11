package systems.zlink.samples.kotlin.bingo.server.api.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

@ZLinkHandlerGroup("api")
class MatchBingoHandler(
    private val client: ZLinkClient,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
    override fun handleAsync(
        request: MatchBingoApiReq,
        context: ZLinkRequestContext,
    ) = coroutines.completionStage {
        val allocated = client
            .requestToChannel(SampleNames.PlayChannel, AllocateBingoRoomReq(request.actorId, request.mode))
            .timeout(SampleTimings.RequestTimeout)
            .submit(AllocateBingoRoomRes::class.java)
            .await()

        MatchBingoApiRes(allocated.roomId)
    }
}
