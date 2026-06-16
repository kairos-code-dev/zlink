package systems.zlink.samples.shoppingmallcheckout.server.registry;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.samples.shoppingmallcheckout.server.configuration.SampleTopology;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = RegistryApplication.class)
public final class RegistryApplication {
    private RegistryApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(RegistryApplication.class)
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
