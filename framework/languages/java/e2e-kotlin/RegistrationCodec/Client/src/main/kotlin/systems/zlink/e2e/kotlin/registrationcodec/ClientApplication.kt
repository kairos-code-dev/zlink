package systems.zlink.e2e.kotlin.registrationcodec

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.registrationcodec.client.support.ClientOptions
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
    fun clientOptions(args: ApplicationArguments): ClientOptions =
        ClientOptions.parse(args.sourceArgs)

    @Bean
    fun clientFramework(clientOptions: ClientOptions): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.codecs().use(ZLinkMessagePackCodec.forPayloadTypes(::isPackedType))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("${clientOptions.logDir}/client-flow.log")
                .traceLabel("kotlin-rc-client")
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(clientOptions.serverEndpoint)
        }

    @Bean
    fun clientScenario(client: ZLinkClient, json: ObjectMapper, clientOptions: ClientOptions): ClientScenario =
        ClientScenario(client, json, clientOptions)
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
