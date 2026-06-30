package systems.zlink.samples.deliverydispatch.client;

import java.net.URI;
import java.time.Duration;
import systems.zlink.httpclient.ZLinkHttpClient;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.client.configuration.SampleTopology;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;

public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) throws Exception {
        try (ZLinkHttpClient api = ZLinkHttpClient.create(SampleTopology.ApiHttpUrl).build()) {
            ZLinkStreamConnector customer = createConnector();
            try {
                new DeliveryDispatchClientScenario(api).run(customer);
            } finally {
                customer.close().await();
            }
        }
        System.out.println("deliverydispatch=completed");
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
            2.0));
    }
}
