package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.notifications

import kotlinx.coroutines.future.await
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEvent
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEventKind
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameEndedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameStartedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoNumberDrawnNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoStateNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.PlayerJoinedNotify

class BingoNotificationPublisher {
    suspend fun publish(
        events: List<BingoRoomEvent>,
        actorResolver: (String) -> PlayerActor?,
    ) {
        for (event in events) {
            publish(event, actorResolver(event.recipientActorId))
        }
    }

    private suspend fun publish(
        event: BingoRoomEvent,
        recipient: PlayerActor?,
    ) {
        if (recipient == null) {
            return
        }
        when (event.kind) {
            BingoRoomEventKind.PLAYER_JOINED ->
                recipient.context().boundSession()
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
                    .submit()
                    .await()

            BingoRoomEventKind.GAME_STARTED ->
                recipient.context().boundSession()
                    .send(BingoGameStartedNotify(event.state))
                    .submit()
                    .await()

            BingoRoomEventKind.NUMBER_DRAWN ->
                recipient.context().boundSession()
                    .send(
                        BingoNumberDrawnNotify(
                            event.state.roomId,
                            event.state.drawSeq,
                            event.drawnNumber,
                            event.state,
                        ),
                    )
                    .submit()
                    .await()

            BingoRoomEventKind.STATE ->
                recipient.context().boundSession()
                    .send(BingoStateNotify(event.state))
                    .submit()
                    .await()

            BingoRoomEventKind.GAME_ENDED ->
                recipient.context().boundSession()
                    .send(BingoGameEndedNotify(event.state))
                    .submit()
                    .await()
        }
    }
}
