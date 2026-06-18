package systems.zlink.samples.supportchat.client;

import java.net.URI;
import java.time.Duration;
import systems.zlink.samples.supportchat.client.configuration.SampleTimings;
import systems.zlink.samples.supportchat.client.configuration.SampleTopology;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        ZLinkStreamConnector customer = createClient();
        ZLinkStreamConnector agent = createClient();
        ZLinkStreamConnector reconnectingCustomer = createClient();
        ZLinkStreamConnector waitingCustomer = createClient();
        ZLinkStreamConnector closingCustomer = createClient();
        ZLinkStreamConnector closingAgent = createClient();
        try {
            new SupportChatClientScenario().run(
                customer,
                agent,
                reconnectingCustomer,
                waitingCustomer,
                closingCustomer,
                closingAgent);
        } finally {
            customer.close().await();
            agent.close().await();
            reconnectingCustomer.close().await();
            waitingCustomer.close().await();
            closingCustomer.close().await();
            closingAgent.close().await();
        }
        System.out.println("supportchat=completed");
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
            ZLinkProtobufCodec.defaultCodec()));
    }
}
