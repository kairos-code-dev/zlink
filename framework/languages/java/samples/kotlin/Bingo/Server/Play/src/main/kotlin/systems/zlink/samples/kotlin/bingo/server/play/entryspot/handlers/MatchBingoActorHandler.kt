package systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineEntrySpotActorRequestHandler
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.entryspot.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoRes

class MatchBingoActorHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineEntrySpotActorRequestHandler<
    BingoEntrySpot,
    PlayerActor,
    MatchBingoReq,
    MatchBingoRes,
    >(coroutines) {
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
            ),
        )
            .packetName("MatchBingoApiReq")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(MatchBingoApiRes::class.java)
            .await()
        if (cancellationToken.isCancellationRequested) {
            throw IllegalStateException("MatchBingoReq was cancelled")
        }
        val joined = actor.context()
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
            .await()
        if (cancellationToken.isCancellationRequested) {
            throw IllegalStateException("MatchBingoReq was cancelled")
        }
        return MatchBingoRes(matched.roomId, joined.reply().state)
    }
}
