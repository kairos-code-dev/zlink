package systems.zlink.samples.kotlin.bingo.client

import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.samples.kotlin.bingo.client.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameEndedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameStartedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoNumberDrawnNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoPlayerState
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.PlayerJoinedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardRes

class BingoClientScenario {
    suspend fun run(
        client1: ZLinkKotlinStreamConnector,
        client2: ZLinkKotlinStreamConnector,
    ) = coroutineScope {
        client1.connect().await()
        val client1Auth = client1.request(AuthenticateReq("player-1")).await<AuthenticateRes>()
        ensure(client1Auth.actorId == "player-1")

        val client1Match = client1.request(MatchBingoReq("two-player")).await<MatchBingoRes>()
        ensure(client1Match.state.status == "WaitingForPlayers")
        ensure(client1Match.state.hostActorId == client1Auth.actorId)
        ensure(client1.receivedCount(SampleNames.PlayerJoinedPacket) == 0)

        val client1SawClient2Join = client1.waitFor<PlayerJoinedNotify>()
            .where { message -> message.payload().actorId == "player-2" }
            .let { wait -> async { wait.await() } }
        val client1Started = async { client1.waitFor<BingoGameStartedNotify>().await() }
        val client2Started = async { client2.waitFor<BingoGameStartedNotify>().await() }

        client2.connect().await()
        val client2Auth = client2.request(AuthenticateReq("player-2")).await<AuthenticateRes>()
        ensure(client2Auth.actorId == "player-2")
        ensure(client2Auth.actorId != client1Auth.actorId)

        val client2Match = client2.request(MatchBingoReq("two-player")).await<MatchBingoRes>()
        ensure(client2Match.roomId == client1Match.roomId)
        ensure(client2Match.state.status == "Running")

        val join = client1SawClient2Join.await().payload()
        ensure(join.actorId == client2Auth.actorId)
        ensure(client2.receivedCount(SampleNames.PlayerJoinedPacket) == 0)
        ensure(client1Started.await().payload().state.status == "Running")
        ensure(client2Started.await().payload().state.status == "Running")

        val client2Card = client2.request(SubmitBingoCardReq(client2Match.roomId, BingoClientCards.Player2)).await<SubmitBingoCardRes>()
        ensure(client2Card.state.status == "Running")

        val client1Ended = async { client1.waitFor<BingoGameEndedNotify>().await() }
        val client2Ended = async { client2.waitFor<BingoGameEndedNotify>().await() }
        var client1NextDraw = client1.waitFor<BingoNumberDrawnNotify>()
            .where { message -> message.payload().drawSeq == 1 }
            .let { wait -> async { wait.await() } }
        var client2NextDraw = client2.waitFor<BingoNumberDrawnNotify>()
            .where { message -> message.payload().drawSeq == 1 }
            .let { wait -> async { wait.await() } }

        val client1Card = client1.request(SubmitBingoCardReq(client1Match.roomId, BingoClientCards.Player1)).await<SubmitBingoCardRes>()
        ensure(client1Card.state.status == "Running")

        val drawnNumbers = mutableListOf<BingoNumberDrawnNotify>()
        for (drawSeq in 1..15) {
            val client1Drawn = client1NextDraw.await().payload()
            val client2Drawn = client2NextDraw.await().payload()
            drawnNumbers += client1Drawn
            ensure(client1Drawn.drawSeq == drawSeq)
            ensure(client2Drawn.drawSeq == drawSeq)
            ensure(client2Drawn.number == client1Drawn.number)

            if (client1Drawn.state.status == "Finished") {
                break
            }
            client1NextDraw = client1.waitFor<BingoNumberDrawnNotify>()
                .where { message -> message.payload().drawSeq == drawSeq + 1 }
                .let { wait -> async { wait.await() } }
            client2NextDraw = client2.waitFor<BingoNumberDrawnNotify>()
                .where { message -> message.payload().drawSeq == drawSeq + 1 }
                .let { wait -> async { wait.await() } }
        }
        ensure(drawnNumbers.isNotEmpty())
        ensure(drawnNumbers.last().state.status == "Finished")

        val client1Result = client1Ended.await().payload().state
        val client2Result = client2Ended.await().payload().state
        ensure(client1Result.status == "Finished")
        ensure(client2Result.status == "Finished")
        ensure(client2Result.drawnNumbers == client1Result.drawnNumbers)
        ensure(client2Result.winners == client1Result.winners)
        ensure(client2Result.players.map(BingoPlayerState::actorId) ==
            client1Result.players.map(BingoPlayerState::actorId))
        ensure(client1Result.drawnNumbers == drawnNumbers.map(BingoNumberDrawnNotify::number))
        ensure(client1Result.winners == listOf(client1Auth.actorId))
        ensure(client1Result.players.all { player -> player.card.size == 9 })
        ensure(client1Result.players.all { player -> player.marks[4] })
    }
}

private fun ensure(condition: Boolean) {
    if (!condition) {
        throw IllegalStateException("Ensure failed")
    }
}

private object BingoClientCards {
    val Player1: List<Int> = listOf(1, 2, 3, 4, 0, 6, 7, 8, 9)
    val Player2: List<Int> = listOf(10, 11, 12, 13, 0, 14, 4, 5, 6)
}
