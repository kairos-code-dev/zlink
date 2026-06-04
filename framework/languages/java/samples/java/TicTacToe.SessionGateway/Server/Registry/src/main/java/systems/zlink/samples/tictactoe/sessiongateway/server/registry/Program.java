package systems.zlink.samples.tictactoe.sessiongateway.server.registry;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = Program.class)
public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        start(args);
    }

    public static AutoCloseable start(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
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
