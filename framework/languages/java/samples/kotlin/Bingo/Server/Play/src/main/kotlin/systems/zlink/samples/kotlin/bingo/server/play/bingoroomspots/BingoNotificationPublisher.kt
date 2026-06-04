package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameEndedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameStartedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoNumberDrawnNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoStateNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.PlayerJoinedNotify

class BingoNotificationPublisher {
    fun publishAsync(events: List<BingoRoomEvent>): CompletionStage<Void> {
        var stage: CompletionStage<Void> = CompletableFuture.completedFuture(null)
        for (event in events) {
            stage = stage.thenCompose { publishAsync(event) }
        }
        return stage
    }

    private fun publishAsync(event: BingoRoomEvent): CompletionStage<Void> =
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
                    .packetName(SampleNames.PlayerJoinedPacket)
                    .submitAsync()

            BingoRoomEventKind.GAME_STARTED ->
                event.recipient.context().boundSession()
                    .send(BingoGameStartedNotify(event.state))
                    .packetName(SampleNames.GameStartedPacket)
                    .submitAsync()

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
                    .packetName(SampleNames.NumberDrawnPacket)
                    .submitAsync()

            BingoRoomEventKind.STATE ->
                event.recipient.context().boundSession()
                    .send(BingoStateNotify(event.state))
                    .packetName(SampleNames.StatePacket)
                    .submitAsync()

            BingoRoomEventKind.GAME_ENDED ->
                event.recipient.context().boundSession()
                    .send(BingoGameEndedNotify(event.state))
                    .packetName(SampleNames.GameEndedPacket)
                    .submitAsync()
        }
}
