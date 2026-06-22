package systems.zlink.samples.kotlin.supportchat.server.api

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [ApiServerApplication::class],
)
class ApiServerApplication {
    @Bean
    fun apiFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(ApiServerApplication::class.java)
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("SUPPORTCHAT_LOG_DIR") ?: "logs") + "/flow-api.log")
                traceNodeId("api")
            }
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(SampleTopology.ApiChannelEndpoint)
                .addHandlerGroup("api")
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableClient()
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(ApiServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
