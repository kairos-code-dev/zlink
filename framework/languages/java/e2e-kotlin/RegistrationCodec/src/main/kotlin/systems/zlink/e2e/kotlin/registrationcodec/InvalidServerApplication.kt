package systems.zlink.e2e.kotlin.registrationcodec

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.registrationcodec.handlers.ManualRequestHandler
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrationcodec.invalid"],
)
class InvalidServerApplication {
    @Bean fun scenarioState(): ScenarioState = ScenarioState()

    @Bean
    fun invalidFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.codecs().addJson()
            val channel = options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_SERVER_ENDPOINT"))
            channel.addRequestHandler(ManualRequestHandler::class.java, EchoManualRequest::class.java, EchoReply::class.java, "DuplicatePacket")
            channel.addRequestHandler(ManualRequestHandler::class.java, EchoManualRequest::class.java, EchoReply::class.java, "DuplicatePacket")
        }

    @Bean fun manualRequestHandler(state: ScenarioState): ManualRequestHandler =
        ManualRequestHandler(state)
}

fun runInvalidServerApplication(vararg args: String): AutoCloseable {
    val builder = SpringApplicationBuilder(InvalidServerApplication::class.java)
        .web(WebApplicationType.NONE)
    builder.application().setKeepAlive(true)
    val context = builder.run(*args)
    return AutoCloseable { context.close() }
}
