package systems.zlink.samples.bingo.client;

import java.net.URI;
import java.time.Duration;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.protobuf.ZLinkStreamProtobuf;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        ZLinkStreamConnector client1 = createClient();
        ZLinkStreamConnector client2 = createClient();
        try {
            new BingoClientApp().run(client1, client2);
        } finally {
            client1.close().await();
            client2.close().await();
        }
        System.out.println("Bingo client self-check passed");
    }

    private static ZLinkStreamConnector createClient() {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(SampleTopology.StreamEndpoint),
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
            ZLinkStreamProtobuf.codec()));
    }
}
