package systems.zlink.samples.tictactoe.sessiongateway.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers.PlayNotificationRelay;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;



@EnableZLinkFramework
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
            options.useDiscovery(discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint));
            SessionServer.configureRelayNode(options);
            SessionServer.configure(options);
        };
    }

    @Bean
    PlayerSessionDirectory playerSessionDirectory() {
        return new PlayerSessionDirectory();
    }

    @Bean
    PlayNotificationRelay playNotificationRelay(PlayerSessionDirectory sessions) {
        return new PlayNotificationRelay(sessions);
    }
}
