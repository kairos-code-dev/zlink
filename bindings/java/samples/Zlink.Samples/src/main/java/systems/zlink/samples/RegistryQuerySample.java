package systems.zlink.samples;

import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.service.registry.RegistryQueryClient;
public final class RegistryQuerySample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String channelName = "sample";
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();
        String serviceEndpoint = SampleSupport.tcpEndpoint();
        Context ctx = new Context();
        Registry registry = null;
        Discovery discovery = null;
        RegistryQueryClient query = null;
        PubSocket provider = null;
        try {
            registry = new Registry(ctx);
            discovery = new Discovery(ctx, AutoConnectType.FANOUT, channelName);
            query = new RegistryQueryClient(ctx);
            provider = new PubSocket(ctx);
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            provider.attachDiscovery(discovery);
            provider.bind(serviceEndpoint);
            query.connect(registryRouter);
            final RegistryQueryClient finalQuery = query;

            SampleSupport.waitUntil("registry query sample",
                () -> {
                    try {
                        return finalQuery.snapshot().stream()
                            .anyMatch(entry -> channelName.equals(entry.channelName()));
                    } catch (RuntimeException ex) {
                        return false;
                    }
                });

            System.out.println("[registry-query] service: \"sample\" -> snapshot: found");
        } finally {
            SampleSupport.closeQuietly(provider);
            SampleSupport.closeQuietly(query);
            SampleSupport.closeQuietly(discovery);
            SampleSupport.closeQuietly(registry);
            SampleSupport.closeQuietly(ctx);
        }
    }
}
