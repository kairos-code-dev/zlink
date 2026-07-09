package systems.zlink.samples.kotlin.supportchat.server.support

fun main(args: Array<String>) {
    val app = SupportApplication.run(args)
    Runtime.getRuntime().addShutdownHook(Thread { app.close() })
    Thread.currentThread().join()
}
