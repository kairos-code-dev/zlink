package systems.zlink.e2e.discoveryregistryha.embedded;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRegistryLifecycle;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class EmbeddedApplication {
    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EmbeddedOptions embeddedOptions() {
        return EmbeddedOptions.fromEnv();
    }

    @Bean
    EmbeddedEvidenceStore embeddedEvidenceStore(EmbeddedOptions options) {
        return new EmbeddedEvidenceStore(options);
    }

    @Bean
    ZLinkEmbeddedRegistryOptions registryOptions(EmbeddedOptions embedded) {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setRegistryId(embedded.registryId());
        options.setPubEndpoint(embedded.registryPubEndpoint());
        options.setRouterEndpoint(embedded.registryRouterEndpoint());
        options.setHeartbeatInterval(Duration.ofMillis(250));
        options.setHeartbeatTimeout(Duration.ofSeconds(2));
        options.setBroadcastInterval(Duration.ofMillis(250));
        for (String peer : embedded.peerPubEndpoints()) {
            options.addPeer(peer);
        }
        return options;
    }

    @Bean
    ZLinkFrameworkConfigurer embeddedFramework(
        EmbeddedOptions options,
        EmbeddedEvidenceStore evidence) {
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
                .setRoutingId(RoutingId.from(evidence.rid()))
                .addHandlerGroup(Contracts.HANDLER_GROUP);
        };
    }

    @Bean
    ProfileReqHandler profileRequestHandler(EmbeddedEvidenceStore evidence) {
        return new ProfileReqHandler(evidence);
    }

    @Bean
    ZLinkRegistryLifecycle zlinkRegistryLifecycle(
        ZLinkEmbeddedRegistryOptions options,
        ZLinkBackendAdapterFactory backendAdapterFactory) {
        return new ZLinkRegistryLifecycle(options, backendAdapterFactory);
    }

    @Bean
    EmbeddedEndpoints embeddedEndpoints(
        EmbeddedOptions options,
        EmbeddedEvidenceStore evidence,
        ZLinkRegistryLifecycle query,
        ObjectMapper json) {
        return new EmbeddedEndpoints(options, evidence, query, json);
    }
}
