package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

import kotlinx.coroutines.future.await
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameEndedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameStartedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoNumberDrawnNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoStateNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.PlayerJoinedNotify

class BingoNotificationPublisher {
    suspend fun publish(events: List<BingoRoomEvent>) {
        for (event in events) {
            publish(event)
        }
    }

    private suspend fun publish(event: BingoRoomEvent) {
        when (event.kind) {
            BingoRoomEventKind.PLAYER_JOINED ->
                event.recipient.context().boundSession()
                    .send(
                        PlayerJoinedNotify(
                            event.state.roomId,
                            event.joinedActorId!!,
                            event.joinedDisplayName!!,
                            event.seat,
                            event.host,
                            event.state,
                        ),
                    )
                    .submitAsync()
                    .await()

            BingoRoomEventKind.GAME_STARTED ->
                event.recipient.context().boundSession()
                    .send(BingoGameStartedNotify(event.state))
                    .submitAsync()
                    .await()

            BingoRoomEventKind.NUMBER_DRAWN ->
                event.recipient.context().boundSession()
                    .send(
                        BingoNumberDrawnNotify(
                            event.state.roomId,
                            event.state.drawSeq,
                            event.drawnNumber,
                            event.state,
                        ),
                    )
                    .submitAsync()
                    .await()

            BingoRoomEventKind.STATE ->
                event.recipient.context().boundSession()
                    .send(BingoStateNotify(event.state))
                    .submitAsync()
                    .await()

            BingoRoomEventKind.GAME_ENDED ->
                event.recipient.context().boundSession()
                    .send(BingoGameEndedNotify(event.state))
                    .submitAsync()
                    .await()
        }
    }
}
