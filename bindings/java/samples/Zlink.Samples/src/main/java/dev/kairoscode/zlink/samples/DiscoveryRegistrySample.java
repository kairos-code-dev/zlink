package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.ConfigException;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.RegistryQueryClient;
import dev.kairoscode.zlink.service.registry.AutoConnectType;
import dev.kairoscode.zlink.Context;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class DiscoveryRegistrySample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String serviceName = "sample";
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();
        String serviceEndpoint = SampleSupport.tcpEndpoint();
        Context ctx = new Context();
        Registry registry = null;
        Discovery providerDiscovery = null;
        RegistryQueryClient query = null;
        PubSocket provider = null;
        try {
            registry = new Registry(ctx);
            providerDiscovery = new Discovery(ctx, AutoConnectType.FANOUT, serviceName);
            query = new RegistryQueryClient(ctx);
            provider = new PubSocket(ctx);
            registry.bind(registryPub, registryRouter);
            registry.setBroadcastInterval(Duration.ofMillis(50));
            providerDiscovery.connectRegistry(registryRouter);
            query.connect(registryRouter);
            final RegistryQueryClient queryView = query;
            final Discovery providerDiscoveryView = providerDiscovery;
            final PubSocket providerSocket = provider;
            CountDownLatch completed = new CountDownLatch(2);
            AtomicReference<Throwable> failure = new AtomicReference<>();
            Thread providerThread = new Thread(() -> {
                try {
                    providerSocket.attachDiscovery(providerDiscoveryView);
                    providerSocket.bind(serviceEndpoint);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                } finally {
                    completed.countDown();
                }
            }, "discovery-registry-provider");
            Thread clientThread = new Thread(() -> {
                try {
                    SampleSupport.waitUntil("discovery registry sample",
                        () -> {
                            try {
                                return queryView.snapshot().stream().anyMatch(
                                    entry -> serviceName.equals(
                                        entry.channelName()));
                            } catch (ConfigException transientNotReady) {
                                return false;
                            }
                        });
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                } finally {
                    completed.countDown();
                }
            }, "discovery-registry-client");
            providerThread.start();
            clientThread.start();
            SampleSupport.await(completed, "discovery registry sample");
            if (failure.get() != null) {
                throw new IllegalStateException("discovery registry sample failed",
                    failure.get());
            }

            System.out.println("[discovery-registry] service: \"sample\" -> discovered");
        } finally {
            SampleSupport.closeQuietly(provider);
            SampleSupport.closeQuietly(query);
            SampleSupport.closeQuietly(providerDiscovery);
            SampleSupport.closeQuietly(registry);
            SampleSupport.closeQuietly(ctx);
        }
    }
}
