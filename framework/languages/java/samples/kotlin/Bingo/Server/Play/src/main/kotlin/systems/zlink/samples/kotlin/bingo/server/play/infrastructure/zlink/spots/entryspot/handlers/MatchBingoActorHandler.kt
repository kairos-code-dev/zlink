package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.kotlin.awaitJoin
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoRes

class MatchBingoActorHandler : ZLinkSuspendingEntrySpotActorRequestHandler<
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
            ),
        )
            .timeout(SampleTimings.RequestTimeout)
            .submit(MatchBingoApiRes::class.java)
            .await()
        val joined = actor.context().joinSpot(
            matched.roomId,
            BingoRoomJoinReq(
                matched.roomId,
                actor.actorId(),
                actor.displayName,
                false,
            ),
        )
            .timeout(SampleTimings.RequestTimeout)
            .awaitJoin(BingoRoomJoinRes::class.java)
        return MatchBingoRes(matched.roomId, joined.reply().state)
    }
}
