package systems.zlink.samples.kotlin.supportchat.server.session

fun main(args: Array<String>) {
    val app = SessionApplication.run(args)
    Runtime.getRuntime().addShutdownHook(Thread { app.close() })
    Thread.currentThread().join()
}
