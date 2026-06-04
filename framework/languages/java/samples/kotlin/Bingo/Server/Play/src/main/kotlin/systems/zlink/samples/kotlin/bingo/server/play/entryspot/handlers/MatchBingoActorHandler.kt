package systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotOutbound
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoRes

class MatchBingoActorHandler(
    private val outbound: ZLinkSpotOutbound,
) {
    @ZLinkSpotActorRequest(packetName = "MatchBingoReq")
    fun handleAsync(
        actor: PlayerActor,
        request: MatchBingoReq,
    ): CompletionStage<MatchBingoRes> {
        return outbound.requestToChannel(
            SampleNames.ApiChannel,
            MatchBingoApiReq(
                actor.actorId(),
                actor.displayName,
                request.mode,
            ),
        )
            .packetName("MatchBingoApiReq")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(MatchBingoApiRes::class.java)
            .thenCompose { matched ->
                actor.context()
                    .joinSpot(
                        RoutingId.fromHex(matched.roomId),
                        BingoRoomJoinReq(
                            matched.roomId,
                            actor.actorId(),
                            actor.displayName,
                        ),
                    )
                    .timeout(SampleTimings.RequestTimeout)
                    .submitAsync(BingoRoomJoinRes::class.java)
                    .thenApply { joined -> MatchBingoRes(matched.roomId, joined.reply().state) }
            }
    }
}
