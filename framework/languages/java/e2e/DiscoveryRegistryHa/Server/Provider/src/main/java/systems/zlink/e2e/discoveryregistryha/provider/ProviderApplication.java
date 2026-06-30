package systems.zlink.e2e.discoveryregistryha.provider;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.e2e.discoveryregistryha.shared.Env;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class ProviderApplication {
    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ProviderOptions providerOptions() {
        return ProviderOptions.fromEnv();
    }

    @Bean
    ProviderEvidenceStore providerEvidenceStore(ProviderOptions options) {
        return new ProviderEvidenceStore(options);
    }

    @Bean
    ZLinkFrameworkConfigurer providerFramework(
        ProviderOptions options,
        ProviderEvidenceStore evidence) {
        return framework -> {
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/" + options.rid() + "-flow.log")
                .traceLabel(options.rid());
            framework.addHandlersFromPackageOf(ProfileReqHandler.class);
            for (String registry : options.discoveryEndpoints()) {
                framework.useDiscovery().addRegistryEndpoint(registry);
            }
            framework.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(options.channelEndpoint())
                .setRoutingId(RoutingId.from(options.rid()))
                .addHandlerGroup(Contracts.HANDLER_GROUP);
        };
    }

    @Bean
    ProfileReqHandler profileRequestHandler(ProviderEvidenceStore evidence) {
        return new ProfileReqHandler(evidence);
    }

    @Bean
    ProviderEndpoints providerEndpoints(
        ProviderOptions options,
        ProviderEvidenceStore evidence,
        ObjectMapper json) {
        return new ProviderEndpoints(options, evidence, json);
    }
}
