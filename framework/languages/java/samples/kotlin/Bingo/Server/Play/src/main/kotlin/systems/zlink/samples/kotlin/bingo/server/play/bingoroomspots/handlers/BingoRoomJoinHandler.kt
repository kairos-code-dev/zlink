package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkSpotActorJoin
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes

class BingoRoomJoinHandler {
    @ZLinkSpotActorJoin
    fun handleAsync(
        actor: PlayerActor,
        request: BingoRoomJoinReq,
    ): CompletionStage<BingoRoomJoinRes> {
        return actor.context()
            .getSpot(BingoRoomSpot::class.java)
            .joinAsync(actor, request)
    }
}
