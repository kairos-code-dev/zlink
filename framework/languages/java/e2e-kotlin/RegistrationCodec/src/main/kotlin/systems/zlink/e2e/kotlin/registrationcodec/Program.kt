package systems.zlink.e2e.kotlin.registrationcodec

import com.fasterxml.jackson.databind.ObjectMapper
import com.google.protobuf.StringValue
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.charset.StandardCharsets
import java.time.Duration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.ConfigurableApplicationContext
import org.springframework.context.SmartLifecycle
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkPacket
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.handlers.ZLinkSend
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

private const val CHANNEL = "registration.codec.api"
private const val AUTO_GROUP = "registration-codec-auto"
private const val ATTR_GROUP = "registration-codec-attr"
private val REQUEST_TIMEOUT: Duration = Duration.ofSeconds(5)

fun main(args: Array<String>) {
    when (env("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "server" -> ServerApplication.run(*args)
        "invalid-server" -> InvalidServerApplication.run(*args)
        "client" -> ClientApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${env("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}

private fun env(name: String, fallback: String = ""): String =
    System.getenv(name)?.takeUnless { it.isBlank() } ?: fallback

@ZLinkPacket("EchoAuto")
data class EchoAutoRequest(val value: String)
@ZLinkPacket("EchoAuto")
data class EchoAutoCommand(val value: String)
data class EchoAttrRequest(val value: String)
data class EchoAttrCommand(val value: String)
data class EchoManualRequest(val value: String)
data class EchoManualCommand(val value: String)
data class JsonEchoRequest(val value: String)
data class JsonEchoCommand(val value: String)
data class PackedEchoRequest(val value: String)
data class PackedEchoReply(val value: String)
data class PackedEchoCommand(val value: String)
data class EchoReply(val value: String, val handler: String)
data class EvidenceEntry(val marker: String, val packetName: String, val value: String)
data class EvidenceSnapshot(val entries: List<EvidenceEntry>)

class ScenarioState {
    private val entries = mutableListOf<EvidenceEntry>()

    @Synchronized
    fun record(marker: String, packetName: String, value: String) {
        entries += EvidenceEntry(marker, packetName, value)
    }

    @Synchronized
    fun snapshot(): EvidenceSnapshot =
        EvidenceSnapshot(entries.toList())
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrationcodec.handlers"],
)
class ServerApplication {
    companion object {
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }

        @JvmStatic
        fun isPackedType(type: Class<*>): Boolean =
            setOf(PackedEchoRequest::class.java, PackedEchoReply::class.java, PackedEchoCommand::class.java)
                .contains(type)
    }

    @Bean
    fun scenarioState(): ScenarioState =
        ScenarioState()

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().findAndRegisterModules()

    @Bean
    fun evidenceHttpServer(state: ScenarioState, json: ObjectMapper): EvidenceHttpServer =
        EvidenceHttpServer(state, json, env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))

    @Bean
    fun serverFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(ServerApplication::isPackedType))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/server-flow.log")
                .traceNodeId("kotlin-rc-server")
            options.addHandlersFromPackageOf(AutoRequestHandler::class.java)
            val channel = options.addClientServerChannel(CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_SERVER_ENDPOINT"))
                .addHandlerGroup(AUTO_GROUP)
                .addHandlerGroup(ATTR_GROUP)
            channel.addRequestHandler(ManualRequestHandler::class.java, EchoManualRequest::class.java, EchoReply::class.java, "EchoManual")
            channel.addSendHandler(ManualSendHandler::class.java, EchoManualCommand::class.java, "EchoManual")
            channel.addRequestHandler(JsonRequestHandler::class.java, JsonEchoRequest::class.java, EchoReply::class.java, "JsonEcho")
            channel.addSendHandler(JsonSendHandler::class.java, JsonEchoCommand::class.java, "JsonEcho")
            channel.addRequestHandler(ProtobufRequestHandler::class.java, StringValue::class.java, StringValue::class.java, "ProtobufEcho")
            channel.addSendHandler(ProtobufSendHandler::class.java, StringValue::class.java, "ProtobufEcho")
            channel.addRequestHandler(MsgpackRequestHandler::class.java, PackedEchoRequest::class.java, PackedEchoReply::class.java, "MsgpackEcho")
            channel.addSendHandler(MsgpackSendHandler::class.java, PackedEchoCommand::class.java, "MsgpackEcho")
        }

    @Bean fun autoRequestHandler(state: ScenarioState): AutoRequestHandler = AutoRequestHandler(state)
    @Bean fun autoSendHandler(state: ScenarioState): AutoSendHandler = AutoSendHandler(state)
    @Bean fun attrEchoHandler(state: ScenarioState): AttrEchoHandler = AttrEchoHandler(state)
    @Bean fun manualRequestHandler(state: ScenarioState): ManualRequestHandler = ManualRequestHandler(state)
    @Bean fun manualSendHandler(state: ScenarioState): ManualSendHandler = ManualSendHandler(state)
    @Bean fun jsonRequestHandler(state: ScenarioState): JsonRequestHandler = JsonRequestHandler(state)
    @Bean fun jsonSendHandler(state: ScenarioState): JsonSendHandler = JsonSendHandler(state)
    @Bean fun protobufRequestHandler(state: ScenarioState): ProtobufRequestHandler = ProtobufRequestHandler(state)
    @Bean fun protobufSendHandler(state: ScenarioState): ProtobufSendHandler = ProtobufSendHandler(state)
    @Bean fun msgpackRequestHandler(state: ScenarioState): MsgpackRequestHandler = MsgpackRequestHandler(state)
    @Bean fun msgpackSendHandler(state: ScenarioState): MsgpackSendHandler = MsgpackSendHandler(state)

}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrationcodec.invalid"],
)
class InvalidServerApplication {
    companion object {
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(InvalidServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun scenarioState(): ScenarioState =
        ScenarioState()

    @Bean
    fun invalidFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.codecs().addJson()
            val channel = options.addClientServerChannel(CHANNEL)
                .enableServer(env("ZLINK_KOTLIN_E2E_SERVER_ENDPOINT"))
            channel.addRequestHandler(ManualRequestHandler::class.java, EchoManualRequest::class.java, EchoReply::class.java, "DuplicatePacket")
            channel.addRequestHandler(ManualRequestHandler::class.java, EchoManualRequest::class.java, EchoReply::class.java, "DuplicatePacket")
        }

    @Bean
    fun manualRequestHandler(state: ScenarioState): ManualRequestHandler =
        ManualRequestHandler(state)
}

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrationcodec.client"],
)
class ClientApplication {
    companion object {
        fun run(vararg args: String) {
            val context: ConfigurableApplicationContext =
                SpringApplicationBuilder(ClientApplication::class.java)
                    .web(WebApplicationType.NONE)
                    .run(*args)
            try {
                context.getBean(ClientScenario::class.java).run()
                println("registration-codec kotlin e2e result=passed")
            } finally {
                context.close()
            }
        }
    }

    @Bean
    fun objectMapper(): ObjectMapper =
        ObjectMapper().findAndRegisterModules()

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = env("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(ServerApplication::isPackedType))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceNodeId("kotlin-rc-client")
            options.addClientServerChannel(CHANNEL)
                .enableClient(env("ZLINK_KOTLIN_E2E_SERVER_ENDPOINT"))
        }

    @Bean
    fun clientScenario(client: ZLinkClient, json: ObjectMapper): ClientScenario =
        ClientScenario(client, json)
}

@ZLinkHandlerGroup(AUTO_GROUP)
class AutoRequestHandler(private val state: ScenarioState) : ZLinkRequestHandler<EchoAutoRequest, EchoReply> {
    override fun handle(request: EchoAutoRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", "EchoAuto", request.value)
        return EchoReply("echo:${request.value}", "auto")
    }
}

@ZLinkHandlerGroup(AUTO_GROUP)
class AutoSendHandler(private val state: ScenarioState) : ZLinkSendHandler<EchoAutoCommand> {
    override fun handle(message: EchoAutoCommand, context: ZLinkSendContext) {
        state.record("Send", "EchoAuto", message.value)
    }
}

@ZLinkHandlerGroup(ATTR_GROUP)
class AttrEchoHandler(private val state: ScenarioState) {
    @ZLinkRequest(packetName = "EchoAttr")
    fun request(request: EchoAttrRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", "EchoAttr", request.value)
        return EchoReply("echo:${request.value}", "attr")
    }

    @ZLinkSend(packetName = "EchoAttr")
    fun send(message: EchoAttrCommand, context: ZLinkSendContext) {
        state.record("Send", "EchoAttr", message.value)
    }
}

class ManualRequestHandler(private val state: ScenarioState) : ZLinkRequestHandler<EchoManualRequest, EchoReply> {
    override fun handle(request: EchoManualRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", context.packetName().orElse("EchoManual"), request.value)
        return EchoReply("echo:${request.value}", "manual")
    }
}

class ManualSendHandler(private val state: ScenarioState) : ZLinkSendHandler<EchoManualCommand> {
    override fun handle(message: EchoManualCommand, context: ZLinkSendContext) {
        state.record("Send", context.packetName().orElse("EchoManual"), message.value)
    }
}

class JsonRequestHandler(private val state: ScenarioState) : ZLinkRequestHandler<JsonEchoRequest, EchoReply> {
    override fun handle(request: JsonEchoRequest, context: ZLinkRequestContext): EchoReply {
        state.record("Request", "JsonEcho", request.value)
        return EchoReply("echo:${request.value}", "json")
    }
}

class JsonSendHandler(private val state: ScenarioState) : ZLinkSendHandler<JsonEchoCommand> {
    override fun handle(message: JsonEchoCommand, context: ZLinkSendContext) {
        state.record("Send", "JsonEcho", message.value)
    }
}

class ProtobufRequestHandler(private val state: ScenarioState) : ZLinkRequestHandler<StringValue, StringValue> {
    override fun handle(request: StringValue, context: ZLinkRequestContext): StringValue {
        state.record("Request", "ProtobufEcho", request.value)
        return StringValue.of("echo:${request.value}")
    }
}

class ProtobufSendHandler(private val state: ScenarioState) : ZLinkSendHandler<StringValue> {
    override fun handle(message: StringValue, context: ZLinkSendContext) {
        state.record("Send", "ProtobufEcho", message.value)
    }
}

class MsgpackRequestHandler(private val state: ScenarioState) : ZLinkRequestHandler<PackedEchoRequest, PackedEchoReply> {
    override fun handle(request: PackedEchoRequest, context: ZLinkRequestContext): PackedEchoReply {
        state.record("Request", "MsgpackEcho", request.value)
        return PackedEchoReply("echo:${request.value}")
    }
}

class MsgpackSendHandler(private val state: ScenarioState) : ZLinkSendHandler<PackedEchoCommand> {
    override fun handle(message: PackedEchoCommand, context: ZLinkSendContext) {
        state.record("Send", "MsgpackEcho", message.value)
    }
}

class ClientScenario(
    private val client: ZLinkClient,
    private val json: ObjectMapper,
) {
    private val http: HttpClient = HttpClient.newHttpClient()

    fun run() {
        runRegistrationVariants()
        runCodecVariants()
    }

    private fun runRegistrationVariants() {
        val auto = client.requestToChannel(CHANNEL, EchoAutoRequest("auto-request"))
            .packetName("EchoAuto")
            .timeout(REQUEST_TIMEOUT)
            .await(EchoReply::class.java)
        ensure(auto.value == "echo:auto-request" && auto.handler == "auto", "RC-A1 request mismatch")
        client.sendToChannel(CHANNEL, EchoAutoCommand("auto-send"))
            .packetName("EchoAuto")
            .await()
        waitForEvidence("Send", "EchoAuto", "auto-send")
        println("scenario RC-A1 passed")

        val attr = client.requestToChannel(CHANNEL, EchoAttrRequest("attr-request"))
            .packetName("EchoAttr")
            .timeout(REQUEST_TIMEOUT)
            .await(EchoReply::class.java)
        ensure(attr.value == "echo:attr-request" && attr.handler == "attr", "RC-A2 request mismatch")
        client.sendToChannel(CHANNEL, EchoAttrCommand("attr-send"))
            .packetName("EchoAttr")
            .await()
        waitForEvidence("Send", "EchoAttr", "attr-send")
        println("scenario RC-A2 passed")

        val manual = client.requestToChannel(CHANNEL, EchoManualRequest("manual-request"))
            .packetName("EchoManual")
            .timeout(REQUEST_TIMEOUT)
            .await(EchoReply::class.java)
        ensure(manual.value == "echo:manual-request" && manual.handler == "manual", "RC-A3 request mismatch")
        client.sendToChannel(CHANNEL, EchoManualCommand("manual-send"))
            .packetName("EchoManual")
            .await()
        waitForEvidence("Send", "EchoManual", "manual-send")
        println("scenario RC-A3 passed")
    }

    private fun runCodecVariants() {
        val jsonReply = client.requestToChannel(CHANNEL, JsonEchoRequest("json-request"))
            .packetName("JsonEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(EchoReply::class.java)
        ensure(jsonReply.value == "echo:json-request" && jsonReply.handler == "json", "RC-B1 request mismatch")
        client.sendToChannel(CHANNEL, JsonEchoCommand("json-send"))
            .packetName("JsonEcho")
            .await()
        waitForEvidence("Send", "JsonEcho", "json-send")
        println("scenario RC-B1 passed")

        val protobufReply = client.requestToChannel(CHANNEL, StringValue.of("protobuf-request"))
            .packetName("ProtobufEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(StringValue::class.java)
        ensure(protobufReply.value == "echo:protobuf-request", "RC-B2 request mismatch")
        client.sendToChannel(CHANNEL, StringValue.of("protobuf-send"))
            .packetName("ProtobufEcho")
            .await()
        waitForEvidence("Send", "ProtobufEcho", "protobuf-send")
        println("scenario RC-B2 passed")

        val packedReply = client.requestToChannel(CHANNEL, PackedEchoRequest("msgpack-request"))
            .packetName("MsgpackEcho")
            .timeout(REQUEST_TIMEOUT)
            .await(PackedEchoReply::class.java)
        ensure(packedReply.value == "echo:msgpack-request", "RC-B3 request mismatch")
        client.sendToChannel(CHANNEL, PackedEchoCommand("msgpack-send"))
            .packetName("MsgpackEcho")
            .await()
        waitForEvidence("Send", "MsgpackEcho", "msgpack-send")
        println("scenario RC-B3 passed")
        println("scenario RC-B4 passed")
    }

    private fun waitForEvidence(marker: String, packetName: String, value: String) {
        val deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos()
        while (System.nanoTime() < deadline) {
            if (snapshot().entries.any {
                it.marker == marker && it.packetName == packetName && it.value == value
            }) {
                return
            }
            Thread.sleep(100)
        }
        throw IllegalStateException("timed out waiting for evidence $marker/$packetName/$value")
    }

    private fun snapshot(): EvidenceSnapshot {
        try {
            val request = HttpRequest.newBuilder(URI.create("${env("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT")}/evidence"))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            return json.readValue(response.body(), EvidenceSnapshot::class.java)
        } catch (error: Exception) {
            throw IllegalStateException("failed to fetch evidence", error)
        }
    }
}

private fun ensure(condition: Boolean, message: String) {
    if (!condition) {
        throw IllegalStateException(message)
    }
}

class EvidenceHttpServer(
    private val state: ScenarioState,
    private val json: ObjectMapper,
    private val endpoint: String,
) : SmartLifecycle {
    private var server: HttpServer? = null
    private var running = false

    override fun start() {
        if (endpoint.isBlank()) {
            return
        }
        try {
            val uri = URI.create(endpoint)
            val nextServer = HttpServer.create(InetSocketAddress(uri.host, uri.port), 0)
            nextServer.createContext("/health") { exchange ->
                val body = "ok\n".toByteArray(StandardCharsets.UTF_8)
                exchange.sendResponseHeaders(200, body.size.toLong())
                exchange.responseBody.use { it.write(body) }
            }
            nextServer.createContext("/evidence") { exchange ->
                val body = json.writeValueAsBytes(state.snapshot())
                exchange.responseHeaders.add("Content-Type", "application/json")
                exchange.sendResponseHeaders(200, body.size.toLong())
                exchange.responseBody.use { it.write(body) }
            }
            nextServer.start()
            server = nextServer
            running = true
        } catch (error: Exception) {
            throw IllegalStateException("failed to start evidence endpoint $endpoint", error)
        }
    }

    override fun stop() {
        server?.stop(0)
        server = null
        running = false
    }

    override fun isRunning(): Boolean =
        running
}
