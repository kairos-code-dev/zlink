package systems.zlink.samples.kotlin.bingo.server.api.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

@ZLinkHandlerGroup("api")
class MatchBingoHandler(
    private val client: ZLinkClient,
) {
    @ZLinkRequest(packetName = "MatchBingoApiReq")
    fun handleAsync(request: MatchBingoApiReq): CompletionStage<MatchBingoApiRes> {
        return client.requestToChannel(
            SampleNames.PlayChannel,
            AllocateBingoRoomReq(request.actorId, request.mode),
        )
            .packetName("AllocateBingoRoomReq")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(AllocateBingoRoomRes::class.java)
            .thenApply { MatchBingoApiRes(it.roomId) }
    }
}
