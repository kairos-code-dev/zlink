package systems.zlink.samples.kotlin.bingo.client

import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameEndedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameStartedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoNumberDrawnNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinner
import systems.zlink.samples.kotlin.bingo.shared.contracts.PlayerJoinedNotify

class BingoNotificationInbox {
    private val playerJoinedValues = mutableListOf<PlayerJoinedNotify>()
    private val startedValues = mutableListOf<BingoGameStartedNotify>()
    private val drawnValues = mutableListOf<BingoNumberDrawnNotify>()
    private val endedValues = mutableListOf<BingoGameEndedNotify>()

    fun playerJoined(value: PlayerJoinedNotify) {
        playerJoinedValues += value
    }

    fun started(value: BingoGameStartedNotify) {
        startedValues += value
    }

    fun drawn(value: BingoNumberDrawnNotify) {
        drawnValues += value
    }

    fun ended(value: BingoGameEndedNotify) {
        endedValues += value
    }

    fun playerJoined(): List<PlayerJoinedNotify> = playerJoinedValues.toList()

    fun started(): List<BingoGameStartedNotify> = startedValues.toList()

    fun drawn(): List<BingoNumberDrawnNotify> = drawnValues.toList()

    fun ended(): List<BingoGameEndedNotify> = endedValues.toList()

    fun winners(): List<BingoWinner> =
        endedValues.lastOrNull()?.state?.winners?.map(::BingoWinner) ?: emptyList()
}
