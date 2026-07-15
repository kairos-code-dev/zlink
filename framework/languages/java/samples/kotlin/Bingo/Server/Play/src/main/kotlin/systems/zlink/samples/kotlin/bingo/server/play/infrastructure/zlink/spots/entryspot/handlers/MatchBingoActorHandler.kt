package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.handlers

import systems.zlink.contracts.core.RoutingId
import kotlinx.coroutines.future.await
import systems.zlink.framework.kotlin.awaitJoin
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

class MatchBingoActorHandler(private val topology: SampleTopology) : ZLinkSuspendingEntrySpotActorRequestHandler<
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
    ): MatchBingoRes {
        val matched = entrySpot.context().outbound().requestToChannel(
            SampleNames.ApiChannel,
            MatchBingoApiReq(
                actor.actorId(),
                actor.displayName,
                request.mode,
                topology.selectedPlayNodeRid(),
            ),
        )
            .timeout(SampleTimings.RequestTimeout)
            .submit(MatchBingoApiRes::class.java)
            .await()
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
            .awaitJoin(BingoRoomJoinRes::class.java)
        return MatchBingoRes(matched.roomId, joined.reply().state, matched.roomOwnerNodeRid)
    }
}
