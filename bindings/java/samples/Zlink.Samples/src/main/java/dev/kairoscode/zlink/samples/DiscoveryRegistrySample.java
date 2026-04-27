package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.Context;

public final class DiscoveryRegistrySample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String serviceName = "sample";
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();
        String serviceEndpoint = SampleSupport.tcpEndpoint();
        Context ctx = new Context();
        Registry registry = null;
        Discovery discovery = null;
        PubSocket provider = null;
        try {
            registry = new Registry(ctx);
            discovery = new Discovery(ctx, ServiceType.SOCKET, serviceName);
            provider = new PubSocket(ctx);
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            provider.attachDiscovery(discovery);
            provider.bind(serviceEndpoint);
            final Discovery discoveryView = discovery;
            SampleSupport.waitUntil("discovery registry sample",
                () -> discoveryView.memberPeers().stream().anyMatch(
                    entry -> serviceName.equals(entry.serviceName())));

            System.out.println("[discovery-registry] service: \"sample\" -> discovered");
        } finally {
            SampleSupport.closeQuietly(provider);
            SampleSupport.closeQuietly(discovery);
            SampleSupport.closeQuietly(registry);
            SampleSupport.closeQuietly(ctx);
        }
    }
}
