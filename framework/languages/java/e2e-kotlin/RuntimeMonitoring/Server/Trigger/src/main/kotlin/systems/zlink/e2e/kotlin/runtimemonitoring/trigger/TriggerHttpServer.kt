package systems.zlink.e2e.kotlin.runtimemonitoring.trigger

import com.fasterxml.jackson.databind.ObjectMapper
import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import org.springframework.context.SmartLifecycle
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.Env
import systems.zlink.e2e.kotlin.runtimemonitoring.service.EvidenceState
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle
import java.net.InetSocketAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.time.Duration

class TriggerHttpServer(
    private val endpoint: String,
    private val state: EvidenceState,
    private val json: ObjectMapper,
    private val client: ZLinkClient,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        try {
            val uri = URI.create(endpoint)
            val nextServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
            nextServer.createContext("/health") { exchange -> write(exchange, "ok\n") }
            nextServer.createContext("/evidence") { exchange ->
                write(exchange, json.writeValueAsString(state.snapshot()), "application/json")
            }
            nextServer.createContext("/profile/request") { exchange ->
                write(exchange, requestService(), "application/json")
            }
            nextServer.createContext("/profile/request/service-b") { exchange ->
                write(exchange, requestServiceB(), "application/json")
            }
            nextServer.createContext("/validation/registration/duplicate-source") { exchange ->
                write(exchange, MonitoringValidationApplication.verifyDuplicateSocketSource())
            }
            nextServer.createContext("/validation/registration/interval") { exchange ->
                write(exchange, MonitoringValidationApplication.verifyPollingInterval())
            }
            nextServer.createContext("/validation/registration/missing-socket") { exchange ->
                write(exchange, MonitoringValidationApplication.verifyMissingSocketSource())
            }
            nextServer.createContext("/validation/registration/missing-spot") { exchange ->
                write(exchange, MonitoringValidationApplication.verifyMissingSpotSource())
            }
            nextServer.start()
            server = nextServer
            running = true
        } catch (error: Exception) {
            throw IllegalStateException("failed to start trigger endpoint $endpoint", error)
        }
    }

    private fun requestService(): String {
        val reply = client.requestToChannel(
            Contracts.CHANNEL,
            Contracts.WorkRequest("mon-a4-trigger"),
        )
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.WorkReply::class.java)
        return "{\"providerRid\":\"${reply.providerRid}\",\"value\":\"${reply.value}\"}\n"
    }

    private fun requestServiceB(): String {
        val options = DefaultZLinkFrameworkOptions()
        options.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
            .traceLogFile("${Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")}/trigger-service-b-flow.log")
            .traceLabel("kotlin-mon-trigger-service-b")
        options.addClientServerChannel(Contracts.CHANNEL)
            .enableClient(Env.get("ZLINK_KOTLIN_E2E_FILTERED_API_ENDPOINT"))

        val lifecycle = ZLinkFrameworkLifecycle(
            options,
            ZLinkJavaBackendAdapterFactory(),
            ZLinkHandlerFactory.reflection(),
        )
        lifecycle.start()
        try {
            val reply = lifecycle.requestToChannel(
                Contracts.CHANNEL,
                Contracts.WorkRequest("mon-d1-request"),
            )
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkReply::class.java)
            return "{\"providerRid\":\"${reply.providerRid}\",\"value\":\"${reply.value}\"}\n"
        } finally {
            lifecycle.stop()
        }
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean = running

    private fun write(
        exchange: HttpExchange,
        value: String,
        contentType: String = "text/plain; charset=utf-8",
    ) {
        val path = exchange.requestURI.path
        val getAllowed = exchange.requestMethod == "GET" && (path == "/health" || path == "/evidence")
        val postAllowed = exchange.requestMethod == "POST"
        if (!getAllowed && !postAllowed) {
            exchange.sendResponseHeaders(405, -1)
            exchange.close()
            return
        }

        val body = value.toByteArray(StandardCharsets.UTF_8)
        exchange.responseHeaders.add("Content-Type", contentType)
        exchange.sendResponseHeaders(200, body.size.toLong())
        exchange.responseBody.write(body)
        exchange.close()
    }
}
