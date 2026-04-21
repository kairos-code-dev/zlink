package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.Context;
import java.util.concurrent.atomic.AtomicBoolean;

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
        dev.kairoscode.zlink.ServiceMonitor monitor = null;
        try {
            registry = new Registry(ctx);
            discovery = new Discovery(ctx, ServiceType.SOCKET, serviceName);
            provider = new PubSocket(ctx);
            monitor = discovery.monitorOpen();
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            provider.attachDiscovery(discovery);
            AtomicBoolean discovered = new AtomicBoolean(false);
            monitor.onEvent(event -> {
                if (serviceName.equals(event.serviceName())) {
                    discovered.set(true);
                }
            });
            provider.bind(serviceEndpoint);
            SampleSupport.waitUntil("discovery registry sample",
                discovered::get);

            System.out.println("[discovery-registry] service: \"sample\" -> discovered");
        } finally {
            SampleSupport.closeQuietly(monitor);
            SampleSupport.closeQuietly(provider);
            SampleSupport.closeQuietly(discovery);
            SampleSupport.closeQuietly(registry);
            SampleSupport.closeQuietly(ctx);
        }
    }
}
