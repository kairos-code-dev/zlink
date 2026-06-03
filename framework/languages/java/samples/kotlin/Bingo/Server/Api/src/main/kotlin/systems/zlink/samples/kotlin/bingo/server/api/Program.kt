package systems.zlink.samples.kotlin.bingo.server.api

import java.util.concurrent.CountDownLatch

fun main() {
    ApiServerHostFactory.start().use {
        CountDownLatch(1).await()
    }
}
