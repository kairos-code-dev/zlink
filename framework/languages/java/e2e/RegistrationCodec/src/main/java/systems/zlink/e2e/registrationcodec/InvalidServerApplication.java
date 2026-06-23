package systems.zlink.e2e.registrationcodec;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.registrationcodec.handlers.ManualRequestHandler;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrationcodec.invalid")
public final class InvalidServerApplication {
    private InvalidServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(InvalidServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ScenarioState scenarioState() {
        return new ScenarioState();
    }

    @Bean
    ZLinkFrameworkConfigurer invalidFramework() {
        return options -> {
            options.codecs().addJson();
            var channel = options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_SERVER_ENDPOINT"));
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
    ManualRequestHandler manualRequestHandler(ScenarioState state) {
        return new ManualRequestHandler(state);
    }
}
