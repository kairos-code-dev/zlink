package systems.zlink.samples.kotlin.bingo.client

import java.util.concurrent.CompletionException
import kotlinx.coroutines.delay
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomState

class BingoClientApp(
    private val options: BingoClientOptions,
) {
    suspend fun run(drawSequence: List<Int>) {
        val clients = mutableListOf<BingoPlayerClient>()
        for (index in 1..options.playerCount) {
            val client = BingoPlayerClient("player-$index")
            runAction("connect ${client.playerId}") { client.connect() }
            val auth = runAction("authenticate ${client.playerId}") { client.authenticate() }
            require(auth.actorId == client.playerId) { "session authenticate reply mismatch" }
            clients += client
        }

        val firstMatch = runAction("match ${clients.first().playerId}") {
            clients.first().match()
        }
        requireRejected("host start must be rejected before four players join") {
            runAction("premature start ${clients.first().playerId}") {
                clients.first().start(firstMatch.roomId)
            }
        }

        val matches = mutableListOf(firstMatch)
        for (client in clients.drop(1)) {
            matches += runAction("match ${client.playerId}") { client.match() }
        }
        require(matches.map { it.roomId }.distinct().size == 1) {
            "all match requests must return the same room"
        }
        require(matches.first().state.hostActorId == clients.first().playerId) {
            "first joined actor must become host"
        }

        requireRejected("non-host start must be rejected") {
            runAction("non-host start ${clients[1].playerId}") { clients[1].start(firstMatch.roomId) }
        }
        val started = runAction("host start ${clients.first().playerId}") {
            clients.first().start(firstMatch.roomId)
        }
        require(started.state.status == "Running") {
            "host start must put room into Running status"
        }

        val ended = waitForEnded(clients)
        require(ended.status == "Finished") { "room must finish through timer draws" }
        require(ended.winners.size > 1) {
            "deterministic sample must include same-sequence winners"
        }
        require(ended.players.all { it.card.size == 25 }) {
            "each player card must contain 25 cells"
        }
        require(ended.players.all { it.marks[12] }) {
            "center free cell must start marked"
        }
        require(clients.all { it.inbox.started().isNotEmpty() }) {
            "each connector must receive game-start push"
        }
        require(clients.all { it.inbox.drawn().isNotEmpty() }) {
            "each connector must receive draw push"
        }
        require(clients.all { it.inbox.ended().isNotEmpty() }) {
            "each connector must receive game-ended push"
        }
        clients.forEach(BingoPlayerClient::close)
    }

    private suspend fun waitForEnded(clients: List<BingoPlayerClient>): BingoRoomState {
        val deadline = System.nanoTime() + SampleTimings.RequestTimeout.toNanos()
        while (System.nanoTime() < deadline) {
            clients.forEach { it.dispatch() }
            if (clients.all { it.inbox.ended().isNotEmpty() }) {
                return clients
                    .flatMap { it.inbox.ended() }
                    .last()
                    .state
            }
            delay(10)
        }
        throw IllegalStateException("Timed out waiting for BingoGameEndedNotify")
    }

    private suspend fun requireRejected(message: String, action: suspend () -> Unit) {
        try {
            action()
        } catch (ex: CompletionException) {
            return
        } catch (ex: IllegalStateException) {
            return
        }
        throw IllegalStateException(message)
    }

    private suspend fun <T> runAction(action: String, block: suspend () -> T): T {
        try {
            return block()
        } catch (ex: Exception) {
            throw IllegalStateException("Bingo sample failed during $action", ex)
        }
    }
}
