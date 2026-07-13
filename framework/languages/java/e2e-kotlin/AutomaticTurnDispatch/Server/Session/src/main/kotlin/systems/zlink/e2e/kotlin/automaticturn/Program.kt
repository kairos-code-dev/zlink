package systems.zlink.e2e.kotlin.automaticturn

fun main(args: Array<String>) {
    SessionApplication.run(*args).use {
        Thread.currentThread().join()
    }
}
