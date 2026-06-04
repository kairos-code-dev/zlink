package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameRes

class StartBingoGameHandler {
    @ZLinkSpotActorRequest
    fun handleAsync(
        actor: PlayerActor,
        request: StartBingoGameReq,
    ): CompletionStage<StartBingoGameRes> =
        actor.context()
            .getSpot(BingoRoomSpot::class.java)
            .startAsync(actor, request)
}
