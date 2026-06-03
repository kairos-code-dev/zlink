package systems.zlink.samples.kotlin.bingo.server.play

import java.util.concurrent.CountDownLatch

fun main() {
    PlayServerHostFactory.start().use {
        CountDownLatch(1).await()
    }
}
