package systems.zlink.samples.gamequest.client;

import java.net.URI;
import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.gamequest.client.configuration.SampleNames;
import systems.zlink.samples.gamequest.client.configuration.SampleTimings;
import systems.zlink.samples.gamequest.client.configuration.SampleTopology;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = ClientApplication.class)
public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) throws Exception {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ClientApplication.class)
            .web(WebApplicationType.NONE);
        try (var context = builder.run(args)) {
            ZLinkClient channels = context.getBean(ZLinkClient.class);
            ZLinkStreamConnector alice = createConnector(SampleTopology.GameApiAStreamEndpoint);
            ZLinkStreamConnector bob = createConnector(SampleTopology.GameApiBStreamEndpoint);
            try {
                new GameQuestClientScenario(channels).run(alice, bob);
            } finally {
                alice.close().await();
                bob.close().await();
            }
        }
        System.out.println("gamequest=completed");
    }

    @Bean
    ZLinkFrameworkConfigurer clientFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.codecs().addJson();
            options.addClientServerChannel(SampleNames.gameApiActionChannel(SampleNames.ApiA))
                .enableClient(SampleTopology.GameApiAActionEndpoint);
            options.addClientServerChannel(SampleNames.gameApiActionChannel(SampleNames.ApiB))
                .enableClient(SampleTopology.GameApiBActionEndpoint);
        };
    }

    private static ZLinkStreamConnector createConnector(String endpoint) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint),
            ZLinkStreamDispatchMode.AUTO,
            SampleTimings.RequestTimeout,
            2,
            SampleTimings.ConnectTimeout,
            64 * 1024,
            false,
            Duration.ofSeconds(1),
            SampleTimings.RequestTimeout.plusSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            ZLinkStreamJson.codec()));
    }
}
