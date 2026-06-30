package systems.zlink.e2e.kotlin.resiliencelifecycle

fun main(args: Array<String>) {
    RegistryApplication.run(*args).use { _ ->
        Thread.currentThread().join()
    }
}
