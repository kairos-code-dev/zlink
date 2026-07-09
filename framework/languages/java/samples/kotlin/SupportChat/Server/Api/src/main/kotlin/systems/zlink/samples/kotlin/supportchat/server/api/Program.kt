package systems.zlink.samples.kotlin.supportchat.server.api

import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology

fun main(args: Array<String>) {
    val app = ApiApplication.run(args)
    val http = startHttp()
    Runtime.getRuntime().addShutdownHook(
        Thread {
            http.stop(0)
            app.close()
        },
    )
    Thread.currentThread().join()
}

private fun startHttp(): HttpServer {
    val uri = URI.create(SampleTopology.create().apiHttpEndpoint)
    val server = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
    server.createContext("/health") { exchange ->
        val bytes = """{"status":"ok"}""".toByteArray(StandardCharsets.UTF_8)
        exchange.responseHeaders.add("content-type", "application/json")
        exchange.sendResponseHeaders(200, bytes.size.toLong())
        exchange.responseBody.use { it.write(bytes) }
    }
    server.start()
    return server
}
