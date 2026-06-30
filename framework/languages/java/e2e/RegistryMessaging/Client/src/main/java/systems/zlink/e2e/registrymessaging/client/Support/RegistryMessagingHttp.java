package systems.zlink.e2e.registrymessaging.client.Support;

import java.time.Duration;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RegistryMessagingHttp implements AutoCloseable {
    private final ZLinkHttpClient registry;
    private final ZLinkHttpClient providerA;
    private final ZLinkHttpClient providerB;
    private final ZLinkHttpClient workflow;
    private final ZLinkHttpClient discoveryConsumer;
    private final ZLinkHttpClient directConsumer;
    private final ZLinkHttpClient singleConsumer;
    private final ZLinkHttpClient backpressureConsumer;

    public RegistryMessagingHttp() {
        registry = client("ZLINK_JAVA_E2E_REGISTRY_HTTP_URL");
        providerA = client("ZLINK_JAVA_E2E_PROVIDER_A_HTTP_URL");
        providerB = client("ZLINK_JAVA_E2E_PROVIDER_B_HTTP_URL");
        workflow = client("ZLINK_JAVA_E2E_WORKFLOW_HTTP_URL");
        discoveryConsumer = client("ZLINK_JAVA_E2E_DISCOVERY_CONSUMER_HTTP_URL");
        directConsumer = client("ZLINK_JAVA_E2E_DIRECT_CONSUMER_HTTP_URL");
        singleConsumer = client("ZLINK_JAVA_E2E_SINGLE_CONSUMER_HTTP_URL");
        backpressureConsumer = client("ZLINK_JAVA_E2E_BACKPRESSURE_CONSUMER_HTTP_URL");
    }

    public ZLinkHttpClient registry() {
        return registry;
    }

    public ZLinkHttpClient providerA() {
        return providerA;
    }

    public ZLinkHttpClient providerB() {
        return providerB;
    }

    public ZLinkHttpClient workflow() {
        return workflow;
    }

    public ZLinkHttpClient discoveryConsumer() {
        return discoveryConsumer;
    }

    public ZLinkHttpClient directConsumer() {
        return directConsumer;
    }

    public ZLinkHttpClient singleConsumer() {
        return singleConsumer;
    }

    public ZLinkHttpClient backpressureConsumer() {
        return backpressureConsumer;
    }

    @Override
    public void close() {
        registry.close();
        providerA.close();
        providerB.close();
        workflow.close();
        discoveryConsumer.close();
        directConsumer.close();
        singleConsumer.close();
        backpressureConsumer.close();
    }

    private static ZLinkHttpClient client(String envName) {
        return ZLinkHttpClient.create(ClientOptions.get(envName))
            .timeout(Duration.ofMinutes(5))
            .build();
    }
}
