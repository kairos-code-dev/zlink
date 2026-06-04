package systems.zlink.samples.tictactoe.sessiongateway.server.api;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = ApiServer.class)
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
    ZLinkFrameworkOptionsCustomizer apiOptions() {
        return options -> {
            options.useDiscovery(registry ->
                registry.add(SampleTopology.RegistryRouterEndpoint));
            ApiServer.configure(options);
        };
    }
}
