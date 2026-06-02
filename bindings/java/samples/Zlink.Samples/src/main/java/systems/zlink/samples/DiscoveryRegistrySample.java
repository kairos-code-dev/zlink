package systems.zlink.samples;

import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.service.registry.RegistryQueryClient;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

// --8<-- [start:doc]
public final class DiscoveryRegistrySample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String channelName = "sample";
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();
        String serviceEndpoint = SampleSupport.tcpEndpoint();
        Context ctx = Zlink.createContext();
        Registry registry = null;
        Discovery providerDiscovery = null;
        RegistryQueryClient query = null;
        PubSocket provider = null;
        try {
            registry = ctx.createRegistry();
            providerDiscovery = ctx.createDiscovery(AutoConnectType.FANOUT, channelName);
            query = ctx.createRegistryQueryClient();
            provider = ctx.createPubSocket();
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
                                return queryView.topology().stream().anyMatch(
                                    entry -> channelName.equals(
                                        entry.channelName()));
                            } catch (ZlinkConfigException transientNotReady) {
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
// --8<-- [end:doc]
