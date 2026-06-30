package systems.zlink.e2e.pubsub.registry;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.pubsub.registry.Configuration.RegistryOptions;
import systems.zlink.e2e.pubsub.registry.Endpoints.OperationalEndpoints;
import systems.zlink.e2e.pubsub.registry.Infrastructure.EvidenceStore;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;

@SpringBootApplication(proxyBeanMethods = false)
public final class RegistryApplication {
    public AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(RegistryApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    RegistryOptions registryRuntimeOptions() {
        return RegistryOptions.fromEnv();
    }

    @Bean
    ZLinkEmbeddedRegistryOptions registryOptions(RegistryOptions runtimeOptions) {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(runtimeOptions.pubEndpoint());
        options.setRouterEndpoint(runtimeOptions.routerEndpoint());
        return options;
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }

    @Bean
    OperationalEndpoints operationalEndpoints(
        RegistryOptions options,
        EvidenceStore evidence) {
        return new OperationalEndpoints(options, evidence);
    }
}
