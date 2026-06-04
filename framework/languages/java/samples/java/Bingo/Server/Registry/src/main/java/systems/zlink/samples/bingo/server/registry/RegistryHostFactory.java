package systems.zlink.samples.bingo.server.registry;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = RegistryHostFactory.class)
public final class RegistryHostFactory {
    private RegistryHostFactory() {
    }

    public static AutoCloseable start(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(RegistryHostFactory.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkEmbeddedRegistryOptions registryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(SampleTopology.RegistryPubEndpoint);
        options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint);
        return options;
    }
}
