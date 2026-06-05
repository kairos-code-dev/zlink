package systems.zlink.samples.tictactoe.sessiongateway.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = SessionServer.class)
public final class SessionServerHostFactory {
    private SessionServerHostFactory() {
    }

    public static AutoCloseable start(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SessionServerHostFactory.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkOptionsCustomizer sessionOptions() {
        return options -> {
            options.useDiscovery(registry ->
                registry.add(SampleTopology.RegistryRouterEndpoint));
            SessionServer.configureRelayNode(options);
            SessionServer.configure(options);
        };
    }
}
