package systems.zlink.e2e.registrymessaging.provider;

import java.util.concurrent.CompletableFuture;
import java.util.Map;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.registrymessaging.provider.Configuration.ServerOptions;
import systems.zlink.e2e.registrymessaging.provider.Handlers.PayloadRequestHandler;
import systems.zlink.e2e.registrymessaging.provider.Handlers.ProfileCommandHandler;
import systems.zlink.e2e.registrymessaging.provider.Handlers.ProfileRequestHandler;
import systems.zlink.e2e.registrymessaging.provider.Handlers.RoutePingHandler;
import systems.zlink.e2e.registrymessaging.provider.Infrastructure.ScenarioState;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrymessaging.provider")
public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.SERVLET)
            .properties(Map.of("server.port", ServerOptions.get("ZLINK_JAVA_E2E_HTTP_PORT", "0")));
        builder.application().setKeepAlive(true);
        builder.run(args);
    }

    @Bean
    ScenarioState scenarioState() {
        String rid = ServerOptions.get("ZLINK_JAVA_E2E_PROVIDER_RID", "api-a");
        return new ScenarioState(rid, ServerOptions.get("ZLINK_JAVA_E2E_PROVIDER_INSTANCE", rid));
    }

    @Bean
    ZLinkFrameworkConfigurer providerFramework(ScenarioState state) {
        return options -> {
            String logDir = ServerOptions.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + state.providerRid() + "-flow.log")
                .traceLabel("java-rm-" + state.providerRid())
                .setMessageFlowObserver(error -> {
                    if (error.outcome() != ZLinkMessageFlowOutcome.ERROR) {
                        return CompletableFuture.completedFuture(null);
                    }
                    state.record(
                        "dispatch-error",
                        error.errorReason() + "/" + error.errorAction() + "/" + error.packetName());
                    return CompletableFuture.completedFuture(null);
                });
            options.addHandlersFromPackageOf(ProfileRequestHandler.class);
            options.useDiscovery().addRegistryEndpoint(ServerOptions.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));

            String apiEndpoint = ServerOptions.get("ZLINK_JAVA_E2E_API_ENDPOINT");
            if (!apiEndpoint.isBlank()) {
                options.addClientServerChannel(Contracts.API_CHANNEL)
                    .enableServer(apiEndpoint)
                    .enableClient()
                    .setRoutingId(RoutingId.from(state.providerRid()))
                    .addHandlerGroup(Contracts.HANDLER_GROUP);
            }

            String manualEndpoint = ServerOptions.get("ZLINK_JAVA_E2E_API_MANUAL_ENDPOINT");
            if (!manualEndpoint.isBlank()) {
                options.addClientServerChannel("registry.messaging.api.manual")
                    .enableClient(manualEndpoint);
            }

            String workflowEndpoint = ServerOptions.get("ZLINK_JAVA_E2E_WORKFLOW_ENDPOINT");
            if (!workflowEndpoint.isBlank()) {
                options.addClientServerChannel(Contracts.WORKFLOW_CHANNEL)
                    .enableServer(workflowEndpoint)
                    .enableClient()
                    .setRoutingId(RoutingId.from(state.providerRid()))
                    .addHandlerGroup(Contracts.HANDLER_GROUP);
            }

            String routeEndpoint = ServerOptions.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT");
            if (!routeEndpoint.isBlank()) {
                var route = options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                    .enableServer(routeEndpoint)
                    .setRoutingId(RoutingId.from(state.providerRid()));
                route.addRequestHandler(
                    RoutePingHandler.class,
                    Contracts.RoutePing.class,
                    Contracts.RoutePong.class,
                    Contracts.ROUTE_PACKET);
                for (String peer : ServerOptions.get("ZLINK_JAVA_E2E_ROUTE_PEERS").split(",")) {
                    if (!peer.isBlank()) {
                        route.enableClient(peer);
                    }
                }
            }
        };
    }

    @Bean
    ApplicationRunner applyInitialSocketWeight(ZLinkChannelRuntimeOptions runtimeOptions) {
        return ignored -> {
            String weight = ServerOptions.get("ZLINK_JAVA_E2E_API_WEIGHT");
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
    PayloadRequestHandler payloadRequestHandler(ScenarioState state) {
        return new PayloadRequestHandler(state);
    }

    @Bean
    RoutePingHandler routePingHandler(ScenarioState state) {
        return new RoutePingHandler(state);
    }
}
