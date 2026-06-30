package systems.zlink.e2e.kotlin.discoveryregistryha.provider

import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.discoveryregistryha.Contracts
import systems.zlink.e2e.kotlin.discoveryregistryha.ProviderOptions
import systems.zlink.e2e.kotlin.discoveryregistryha.provider.Handlers.WorkRequestHandler
import systems.zlink.e2e.kotlin.discoveryregistryha.provider.Support.ProviderEvidenceStore
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.discoveryregistryha.provider"],
)
class ProviderApplication {
    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ProviderApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun providerOptions(args: ApplicationArguments): ProviderOptions =
        ProviderOptions.parse(args.sourceArgs)

    @Bean
    fun providerState(providerOptions: ProviderOptions): ProviderEvidenceStore =
        ProviderEvidenceStore(providerOptions.providerRid())

    @Bean
    fun providerFramework(
        state: ProviderEvidenceStore,
        providerOptions: ProviderOptions,
    ): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("${providerOptions.logDir()}/${state.providerRid}-flow.log")
                .traceLabel("kotlin-dr-${state.providerRid}")
            options.addHandlersFromPackageOf(WorkRequestHandler::class.java)
            for (registry in providerOptions.registryRouters()) {
                options.useDiscovery().addRegistryEndpoint(registry)
            }
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(providerOptions.apiEndpoint())
                .setRoutingId(RoutingId.from(state.providerRid))
                .addHandlerGroup(Contracts.HANDLER_GROUP)
        }

    @Bean
    fun workRequestHandler(state: ProviderEvidenceStore): WorkRequestHandler =
        WorkRequestHandler(state)
}
