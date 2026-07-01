package systems.zlink.samples.deliverydispatch.client;

import java.net.URI;
import java.time.Duration;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        ZLinkStreamConnector customer = createClient(SampleTopology.CustomerStreamEndpoint);
        ZLinkStreamConnector courierA = createClient(SampleTopology.CourierStreamEndpoint);
        ZLinkStreamConnector courierB = createClient(SampleTopology.CourierStreamEndpoint);
        try {
            new DeliveryDispatchClientScenario().run(customer, courierA, courierB);
        } finally {
            customer.close().await();
            courierA.close().await();
            courierB.close().await();
        }
        System.out.println(SampleNames.CompletedMarker);
    }

    private static ZLinkStreamConnector createClient(String endpoint) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint),
            ZLinkStreamDispatchMode.AUTO,
            SampleTimings.RequestTimeout,
            SampleTimings.RequestTimeout,
            2,
            Duration.ofSeconds(5),
            64 * 1024,
            64 * 1024,
            Integer.MAX_VALUE,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            null,
            null,
            null,
            null));
    }
}
