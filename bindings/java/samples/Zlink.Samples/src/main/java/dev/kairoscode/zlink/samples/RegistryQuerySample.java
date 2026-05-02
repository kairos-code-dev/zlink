package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.RegistryQueryClient;
import dev.kairoscode.zlink.service.registry.AutoConnectType;

public final class RegistryQuerySample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String serviceName = "sample";
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
            discovery = new Discovery(ctx, AutoConnectType.FANOUT, serviceName);
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
                            .anyMatch(entry -> serviceName.equals(entry.channelName()));
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
