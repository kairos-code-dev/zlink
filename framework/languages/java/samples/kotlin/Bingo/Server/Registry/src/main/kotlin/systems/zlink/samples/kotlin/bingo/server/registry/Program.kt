package systems.zlink.samples.kotlin.bingo.server.registry

import java.util.concurrent.CountDownLatch

fun main() {
    RegistryHostFactory.start().use {
        CountDownLatch(1).await()
    }
}
