package systems.zlink.e2e.kotlin.registrationcodec

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrationcodec.client"],
)
class ClientApplication {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(::isPackedType))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceLabel("kotlin-rc-client")
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_SERVER_ENDPOINT"))
        }

    @Bean
    fun clientScenario(client: ZLinkClient, json: ObjectMapper): ClientScenario =
        ClientScenario(client, json)
}

fun isPackedType(type: Class<*>): Boolean =
    type == PackedEchoRequest::class.java ||
        type == PackedEchoReply::class.java ||
        type == PackedEchoCommand::class.java

fun runClientApplication(vararg args: String) {
    val context = SpringApplicationBuilder(ClientApplication::class.java)
        .web(WebApplicationType.NONE)
        .run(*args)
    try {
        context.getBean(ClientScenario::class.java).run()
        println("registration-codec kotlin e2e result=passed")
    } finally {
        context.close()
    }
}
