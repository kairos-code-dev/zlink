// 자립형 가이드 예제: discovery + registry로 서비스 채널을 등록/발견한다.
//   bindings/java/gradlew -p . :kotlin-samples:runDiscoveryRegistrySample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.errors.ZlinkConfigException
import systems.zlink.contracts.service.discovery.Discovery
import systems.zlink.contracts.service.registry.AutoConnectType
import systems.zlink.contracts.service.registry.Registry
import systems.zlink.contracts.service.registry.RegistryQueryClient
import systems.zlink.contracts.sockets.PubSocket
import java.time.Duration
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicReference

fun main() {
// --8<-- [start:doc]
    SampleSupport.ensureNative()
    val channelName = "sample"
    val registryPub = SampleSupport.tcpEndpoint()
    val registryRouter = SampleSupport.tcpEndpoint()
    val serviceEndpoint = SampleSupport.tcpEndpoint()
    val ctx = Zlink.createContext()
    var registry: Registry? = null
    var providerDiscovery: Discovery? = null
    var query: RegistryQueryClient? = null
    var provider: PubSocket? = null
    try {
        registry = ctx.createRegistry()
        providerDiscovery = ctx.createDiscovery(AutoConnectType.FANOUT, channelName)
        query = ctx.createRegistryQueryClient()
        provider = ctx.createPubSocket()
        registry.bind(registryPub, registryRouter)
        registry.setBroadcastInterval(Duration.ofMillis(50))
        providerDiscovery.connectRegistry(registryRouter)
        query.connect(registryRouter)
        val queryView = query
        val providerDiscoveryView = providerDiscovery
        val providerSocket = provider
        val completed = CountDownLatch(2)
        val failure = AtomicReference<Throwable?>()
        val providerThread = Thread({
            try {
                providerSocket.attachDiscovery(providerDiscoveryView)
                providerSocket.bind(serviceEndpoint)
            } catch (ex: Throwable) {
                failure.compareAndSet(null, ex)
            } finally {
                completed.countDown()
            }
        }, "discovery-registry-provider")
        val clientThread = Thread({
            try {
                SampleSupport.waitUntil("discovery registry sample") {
                    try {
                        queryView.topology().stream().anyMatch { entry ->
                            channelName == entry.channelName()
                        }
                    } catch (transientNotReady: ZlinkConfigException) {
                        false
                    }
                }
            } catch (ex: Throwable) {
                failure.compareAndSet(null, ex)
            } finally {
                completed.countDown()
            }
        }, "discovery-registry-client")
        providerThread.start()
        clientThread.start()
        SampleSupport.await(completed, "discovery registry sample")
        failure.get()?.let { throw IllegalStateException("discovery registry sample failed", it) }

        println("[discovery-registry] service: \"sample\" -> discovered")
    } finally {
        SampleSupport.closeQuietly(provider)
        SampleSupport.closeQuietly(query)
        SampleSupport.closeQuietly(providerDiscovery)
        SampleSupport.closeQuietly(registry)
        SampleSupport.closeQuietly(ctx)
    }
// --8<-- [end:doc]
}
