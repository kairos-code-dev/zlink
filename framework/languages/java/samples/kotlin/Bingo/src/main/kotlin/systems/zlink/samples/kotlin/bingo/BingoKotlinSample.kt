package systems.zlink.samples.kotlin.bingo

import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.bingo.client.BingoClientApp
import systems.zlink.samples.kotlin.bingo.client.BingoClientOptions
import systems.zlink.samples.kotlin.bingo.server.api.ApiServerHostFactory
import systems.zlink.samples.kotlin.bingo.server.play.PlayServerHostFactory
import systems.zlink.samples.kotlin.bingo.server.registry.RegistryHostFactory
import systems.zlink.samples.kotlin.bingo.server.session.SessionServerHostFactory

fun main() = runBlocking {
    val drawSequence = listOf(7, 11, 42, 42)
    RegistryHostFactory.start().use {
        ApiServerHostFactory.start().use {
            PlayServerHostFactory.start().use {
                SessionServerHostFactory.start().use {
                    val app = BingoClientApp(BingoClientOptions(playerCount = 4))
                    app.run(drawSequence)
                }
            }
        }
    }
    println("Bingo Kotlin sample self-check passed")
}
