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
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology.SampleSessionNode;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = SessionServer.class)
public final class SessionServerHostFactory {
    private SessionServerHostFactory() {
    }

    public static AutoCloseable start(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SessionServerHostFactory.class)
            .web(WebApplicationType.NONE)
            .initializers(context ->
                context.getBeanFactory().registerSingleton(
                    "sampleSessionNode",
                    sampleSessionNode(args)));
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkOptionsCustomizer sessionOptions(SampleSessionNode sessionNode) {
        return options -> {
            options.useDiscovery(discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint));
            SessionServer.configureRelayNode(options, sessionNode);
            SessionServer.configure(options, sessionNode);
        };
    }

    private static SampleSessionNode sampleSessionNode(String[] args) {
        for (String arg : args) {
            if ("--reconnect".equals(arg)) {
                return SampleTopology.reconnectSession();
            }
        }
        return SampleTopology.primarySession();
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
