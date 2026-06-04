package systems.zlink.samples.bingo.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.bingo.server.session.sessions.BingoSession;
import systems.zlink.samples.bingo.server.session.sessions.handlers.AuthenticateSessionHandler;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = SessionServerHostFactory.class)
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
            options.useDiscovery(discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint));
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> channel.enableClient());
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> channel.enableClient());
            options.addStreamNode(SampleNames.StreamNode, stream -> {
                stream.bind(SampleTopology.StreamEndpoint);
                stream.registerSession(BingoSession.class);
                stream.addSessionPacketHandler(AuthenticateSessionHandler.class);
            });
        };
    }
}
