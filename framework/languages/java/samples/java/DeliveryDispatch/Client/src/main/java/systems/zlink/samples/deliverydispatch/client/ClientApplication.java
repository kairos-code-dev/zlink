package systems.zlink.samples.deliverydispatch.client;

import java.net.URI;
import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleTopology;
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
            ZLinkStreamConnector customer = createConnector();
            try {
                new DeliveryDispatchClientScenario(channels).run(customer);
            } finally {
                customer.close().await();
            }
        }
        System.out.println("deliverydispatch=completed");
    }

    @Bean
    ZLinkFrameworkConfigurer clientFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.codecs().addJson();
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(SampleTopology.ApiChannelEndpoint);
        };
    }

    private static ZLinkStreamConnector createConnector() {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(SampleTopology.SessionStreamEndpoint),
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
