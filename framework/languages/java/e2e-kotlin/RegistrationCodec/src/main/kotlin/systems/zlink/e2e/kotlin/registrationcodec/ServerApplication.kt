package systems.zlink.e2e.kotlin.registrationcodec

import com.fasterxml.jackson.databind.ObjectMapper
import com.google.protobuf.StringValue
import org.springframework.beans.factory.ObjectProvider
import org.springframework.beans.factory.config.ConfigurableBeanFactory
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import org.springframework.context.annotation.Scope
import systems.zlink.e2e.kotlin.registrationcodec.handlers.AttrEchoHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.AutoRequestHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.AutoSendHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.DiLifecycleRequestHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.JsonRequestHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.JsonSendHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.ManualRequestHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.ManualSendHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.MsgpackRequestHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.MsgpackSendHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.ProtobufRequestHandler
import systems.zlink.e2e.kotlin.registrationcodec.handlers.ProtobufSendHandler
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrationcodec.handlers"],
)
class ServerApplication {
    @Bean fun scenarioState(): ScenarioState = ScenarioState()
    @Bean fun objectMapper(): ObjectMapper = ObjectMapper()
    @Bean fun evidenceHttpServer(state: ScenarioState, json: ObjectMapper): EvidenceHttpServer =
        EvidenceHttpServer(state, json, Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))

    @Bean
    fun serverFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            if (Env.get("ZLINK_KOTLIN_E2E_CODEC_MODE") != "json-only") {
                options.codecs().use(ZLinkProtobufCodec.defaultCodec())
                options.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(::isPackedType))
            }
            options.useFilter(FirstOrderFilter::class.java)
            options.useFilter(SecondOrderFilter::class.java)
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/server-flow.log")
                .traceNodeId("kotlin-rc-server")
            options.addHandlersFromPackageOf(AutoRequestHandler::class.java)
            val channel = options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_SERVER_ENDPOINT"))
                .addHandlerGroup(Contracts.AUTO_GROUP)
                .addHandlerGroup(Contracts.ATTR_GROUP)
            channel.addRequestHandler(ManualRequestHandler::class.java, EchoManualRequest::class.java, EchoReply::class.java, "EchoManual")
            channel.addSendHandler(ManualSendHandler::class.java, EchoManualCommand::class.java, "EchoManual")
            channel.addRequestHandler(DiLifecycleRequestHandler::class.java, DiLifecycleRequest::class.java, DiLifecycleReply::class.java, "DiLifecycle")
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
    @Bean fun diLifecycleRequestHandler(
        scoped: ObjectProvider<DiScopedDependency>,
        singleton: DiSingletonDependency,
        state: ScenarioState,
    ): DiLifecycleRequestHandler = DiLifecycleRequestHandler(scoped, singleton, state)
    @Bean fun diSingletonDependency(): DiSingletonDependency = DiSingletonDependency()
    @Bean
    @Scope(ConfigurableBeanFactory.SCOPE_PROTOTYPE)
    fun diScopedDependency(state: ScenarioState): DiScopedDependency = DiScopedDependency(state)
    @Bean fun jsonRequestHandler(state: ScenarioState): JsonRequestHandler = JsonRequestHandler(state)
    @Bean fun jsonSendHandler(state: ScenarioState): JsonSendHandler = JsonSendHandler(state)
    @Bean fun protobufRequestHandler(state: ScenarioState): ProtobufRequestHandler = ProtobufRequestHandler(state)
    @Bean fun protobufSendHandler(state: ScenarioState): ProtobufSendHandler = ProtobufSendHandler(state)
    @Bean fun msgpackRequestHandler(state: ScenarioState): MsgpackRequestHandler = MsgpackRequestHandler(state)
    @Bean fun msgpackSendHandler(state: ScenarioState): MsgpackSendHandler = MsgpackSendHandler(state)
}

fun runServerApplication(vararg args: String): AutoCloseable {
    val builder = SpringApplicationBuilder(ServerApplication::class.java)
        .web(WebApplicationType.NONE)
    builder.application().setKeepAlive(true)
    val context = builder.run(*args)
    return AutoCloseable { context.close() }
}
