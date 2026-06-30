package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoRes

class MatchBingoActorHandler() : ZLinkSuspendingEntrySpotActorRequestHandler<
    BingoEntrySpot,
    PlayerActor,
    MatchBingoReq,
    MatchBingoRes,
    > {
    override suspend fun handle(
        entrySpot: BingoEntrySpot,
        actor: PlayerActor,
        context: ZLinkSpotActorRequestContext,
        request: MatchBingoReq,
        cancellationToken: CancellationToken,
    ): MatchBingoRes {
        val matched = entrySpot.context().outbound().requestToChannel(
            SampleNames.ApiChannel,
            MatchBingoApiReq(
                actor.actorId(),
                actor.displayName,
                request.mode,
                SampleTopology.selectedPlayNodeRid(),
            ),
        )
            .timeout(SampleTimings.RequestTimeout)
            .await(MatchBingoApiRes::class.java)
        if (cancellationToken.isCancellationRequested) {
            throw IllegalStateException("MatchBingoReq was cancelled")
        }
        val joined = actor.context().joinSpot(
            RoutingId.from(matched.roomId),
            BingoRoomJoinReq(
                matched.roomId,
                actor.actorId(),
                actor.displayName,
                false,
            ),
        )
            .timeout(SampleTimings.RequestTimeout)
            .await(BingoRoomJoinRes::class.java)
        if (cancellationToken.isCancellationRequested) {
            throw IllegalStateException("MatchBingoReq was cancelled")
        }
        return MatchBingoRes(matched.roomId, joined.reply().state, matched.roomOwnerNodeRid)
    }
}
