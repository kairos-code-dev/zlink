package systems.zlink.e2e.registrationcodec.invalidduplicate;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.registrationcodec.invalidduplicate.Configuration.ServerOptions;
import systems.zlink.e2e.registrationcodec.invalidduplicate.Handlers.ManualRequestHandler;
import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class InvalidDuplicateApplication {
    public AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(InvalidDuplicateApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean ServerOptions serverOptions() { return ServerOptions.fromEnv(); }

    @Bean
    ZLinkFrameworkConfigurer invalidFramework(ServerOptions options) {
        return framework -> {
            framework.codecs().addJson();
            var channel = framework.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(options.serverEndpoint());
            channel.addRequestHandler(
                ManualRequestHandler.class,
                Contracts.EchoManualRequest.class,
                Contracts.EchoReply.class,
                "DuplicatePacket");
            channel.addRequestHandler(
                ManualRequestHandler.class,
                Contracts.EchoManualRequest.class,
                Contracts.EchoReply.class,
                "DuplicatePacket");
        };
    }

    @Bean
    ManualRequestHandler manualRequestHandler() {
        return new ManualRequestHandler();
    }
}
