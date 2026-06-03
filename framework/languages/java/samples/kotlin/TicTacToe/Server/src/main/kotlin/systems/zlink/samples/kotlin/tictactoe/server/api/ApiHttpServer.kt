package systems.zlink.samples.kotlin.tictactoe.server.api

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.tictactoe.server.api.handlers.CreateGameHttpHandler
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings

class ApiHttpServer private constructor(
    private val server: HttpServer,
    private val client: ZLinkClient,
) : AutoCloseable {
    private val json = ObjectMapper().registerKotlinModule()

    companion object {
        fun start(client: ZLinkClient, settings: SampleSettings): ApiHttpServer {
            val server = HttpServer.create(InetSocketAddress("127.0.0.1", settings.apiHttpPort), 0)
            val api = ApiHttpServer(server, client)
            server.createContext("/games") { exchange ->
                CreateGameHttpHandler.handle(exchange, api.client, api.json)
            }
            server.start()
            return api
        }
    }

    override fun close() {
        server.stop(0)
    }
}
