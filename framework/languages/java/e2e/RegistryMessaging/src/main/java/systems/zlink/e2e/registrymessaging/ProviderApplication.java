package systems.zlink.e2e.registrymessaging;

import java.util.concurrent.CompletableFuture;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.registrymessaging.handlers.ProfileCommandHandler;
import systems.zlink.e2e.registrymessaging.handlers.ProfileRequestHandler;
import systems.zlink.e2e.registrymessaging.handlers.RoutePingHandler;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrymessaging.handlers")
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
    ScenarioState scenarioState() {
        String rid = Env.get("ZLINK_JAVA_E2E_PROVIDER_RID", "api-a");
        return new ScenarioState(rid, Env.get("ZLINK_JAVA_E2E_PROVIDER_INSTANCE", rid));
    }

    @Bean
    ZLinkFrameworkConfigurer providerFramework(ScenarioState state) {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + state.providerRid() + "-flow.log")
                .traceNodeId("java-rm-" + state.providerRid())
                .setMessageDispatchErrorObserver(error -> {
                    state.record(
                        "DispatchError",
                        error.reason() + "/" + error.action() + "/" + error.packetName());
                    return CompletableFuture.completedFuture(null);
                });
            options.addHandlersFromPackageOf(ProfileRequestHandler.class);
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));

            String apiEndpoint = Env.get("ZLINK_JAVA_E2E_API_ENDPOINT");
            if (!apiEndpoint.isBlank()) {
                options.addClientServerChannel(Contracts.API_CHANNEL)
                    .enableServer(apiEndpoint)
                    .serverRoutingId(RoutingId.from(state.providerRid()))
                    .addHandlerGroup(Contracts.HANDLER_GROUP);
            }

            String workflowEndpoint = Env.get("ZLINK_JAVA_E2E_WORKFLOW_ENDPOINT");
            if (!workflowEndpoint.isBlank()) {
                options.addClientServerChannel(Contracts.WORKFLOW_CHANNEL)
                    .enableServer(workflowEndpoint)
                    .serverRoutingId(RoutingId.from(state.providerRid()))
                    .addHandlerGroup(Contracts.HANDLER_GROUP);
            }

            String routeEndpoint = Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT");
            if (!routeEndpoint.isBlank()) {
                options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                    .enableServer(routeEndpoint)
                    .setRoutingId(RoutingId.from(state.providerRid()))
                    .addRequestHandler(
                        RoutePingHandler.class,
                        Contracts.RoutePing.class,
                        Contracts.RoutePong.class,
                        Contracts.ROUTE_PACKET);
            }
        };
    }

    @Bean
    ApplicationRunner applyInitialSocketWeight(ZLinkChannelRuntimeOptions runtimeOptions) {
        return ignored -> {
            String weight = Env.get("ZLINK_JAVA_E2E_API_WEIGHT");
            if (!weight.isBlank()) {
                runtimeOptions
                    .clientServerChannel(Contracts.API_CHANNEL)
                    .configureServerSocket()
                    .weight(Integer.parseInt(weight));
            }
        };
    }

    @Bean
    ProfileRequestHandler profileRequestHandler(ScenarioState state) {
        return new ProfileRequestHandler(state);
    }

    @Bean
    ProfileCommandHandler profileCommandHandler(ScenarioState state) {
        return new ProfileCommandHandler(state);
    }

    @Bean
    RoutePingHandler routePingHandler(ScenarioState state) {
        return new RoutePingHandler(state);
    }
}
