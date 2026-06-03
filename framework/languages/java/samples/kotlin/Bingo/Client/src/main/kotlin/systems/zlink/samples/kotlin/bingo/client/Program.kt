package systems.zlink.samples.kotlin.bingo.client

suspend fun main() {
    BingoClientApp(BingoClientOptions(4)).run(listOf(7, 11, 42, 42))
    println("Bingo client self-check passed")
}
