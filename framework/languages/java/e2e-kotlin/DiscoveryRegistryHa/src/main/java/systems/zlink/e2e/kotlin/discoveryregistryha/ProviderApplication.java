package systems.zlink.e2e.kotlin.discoveryregistryha;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.kotlin.discoveryregistryha.handlers.WorkRequestHandler;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.kotlin.discoveryregistryha.handlers")
public final class ProviderApplication {
    private ProviderApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ProviderApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ProviderState providerState() {
        return new ProviderState(Env.get("ZLINK_KOTLIN_E2E_PROVIDER_RID", "api-a"));
    }

    @Bean
    ZLinkFrameworkConfigurer providerFramework(ProviderState state) {
        return options -> {
            String logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + state.providerRid() + "-flow.log")
                .traceLabel("kotlin-dr-" + state.providerRid());
            options.addHandlersFromPackageOf(WorkRequestHandler.class);
            for (String registry : Env.csv("ZLINK_KOTLIN_E2E_REGISTRY_ROUTERS")) {
                options.useDiscovery().addRegistryEndpoint(registry);
            }
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .setRoutingId(RoutingId.from(state.providerRid()))
                .addHandlerGroup(Contracts.HANDLER_GROUP);
        };
    }

    @Bean
    WorkRequestHandler workRequestHandler(ProviderState state) {
        return new WorkRequestHandler(state);
    }
}
