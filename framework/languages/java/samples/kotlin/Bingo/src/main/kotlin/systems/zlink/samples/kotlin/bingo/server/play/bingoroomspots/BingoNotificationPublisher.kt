package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinner

class BingoNotificationPublisher {
    fun publishWinner(actor: PlayerActor, winners: List<String>, roomId: String) {
        actor.context()
            .boundSession()
            .send(BingoWinner(winners.joinToString(",")))
            .packetName("BingoWinner")
            .metadata("roomId", roomId)
            .submitAsync()
    }
}
