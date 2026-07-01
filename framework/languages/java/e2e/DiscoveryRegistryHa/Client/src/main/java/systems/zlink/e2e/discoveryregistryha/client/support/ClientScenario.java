package systems.zlink.e2e.discoveryregistryha.client.support;

public interface ClientScenario {
    DiscoveryApiResult run(ClientContext context) throws Exception;
}
