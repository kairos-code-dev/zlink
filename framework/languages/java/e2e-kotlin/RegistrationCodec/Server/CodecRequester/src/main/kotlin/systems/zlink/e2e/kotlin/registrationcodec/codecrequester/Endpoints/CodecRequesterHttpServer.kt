package systems.zlink.e2e.kotlin.registrationcodec.codecrequester.endpoints

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import org.springframework.context.SmartLifecycle
import systems.zlink.e2e.kotlin.registrationcodec.codecrequester.CodecRequesterProbe

class CodecRequesterHttpServer(
    private val endpoint: String,
    private val json: ObjectMapper,
    private val requester: CodecRequesterProbe,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        val uri = URI.create(endpoint)
        val httpServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
        httpServer.createContext("/health") { exchange ->
            val body = "ok\n".toByteArray(StandardCharsets.UTF_8)
            exchange.sendResponseHeaders(200, body.size.toLong())
            exchange.responseBody.use { it.write(body) }
        }
        httpServer.createContext("/codec/json/request") { exchange ->
            val body = json.writeValueAsBytes(requester.requestJson())
            exchange.responseHeaders.add("Content-Type", "application/json")
            exchange.sendResponseHeaders(200, body.size.toLong())
            exchange.responseBody.use { it.write(body) }
        }
        httpServer.createContext("/codec/protobuf/request") { exchange ->
            val body = json.writeValueAsBytes(requester.requestProtobuf())
            exchange.responseHeaders.add("Content-Type", "application/json")
            exchange.sendResponseHeaders(200, body.size.toLong())
            exchange.responseBody.use { it.write(body) }
        }
        httpServer.start()
        server = httpServer
        running = true
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean = running
}
