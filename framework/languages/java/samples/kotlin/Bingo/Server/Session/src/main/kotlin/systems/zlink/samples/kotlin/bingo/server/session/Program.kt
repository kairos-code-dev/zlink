package systems.zlink.samples.kotlin.bingo.server.session

import java.util.concurrent.CountDownLatch

fun main() {
    SessionServerHostFactory.start().use {
        CountDownLatch(1).await()
    }
}
