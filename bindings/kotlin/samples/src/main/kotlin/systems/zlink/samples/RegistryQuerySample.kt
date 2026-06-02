// 자립형 가이드 예제: 레지스트리에 등록된 채널을 query client로 조회한다.
//   bindings/java/gradlew -p . :kotlin-samples:runRegistryQuerySample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.discovery.Discovery
import systems.zlink.contracts.service.registry.AutoConnectType
import systems.zlink.contracts.service.registry.Registry
import systems.zlink.contracts.service.registry.RegistryQueryClient
import systems.zlink.contracts.sockets.PubSocket

// --8<-- [start:doc]
fun main() {
    SampleSupport.ensureNative()
    val channelName = "sample"
    val registryPub = SampleSupport.tcpEndpoint()
    val registryRouter = SampleSupport.tcpEndpoint()
    val serviceEndpoint = SampleSupport.tcpEndpoint()
    val ctx = Zlink.createContext()
    var registry: Registry? = null
    var discovery: Discovery? = null
    var query: RegistryQueryClient? = null
    var provider: PubSocket? = null
    try {
        registry = ctx.createRegistry()
        discovery = ctx.createDiscovery(AutoConnectType.FANOUT, channelName)
        query = ctx.createRegistryQueryClient()
        provider = ctx.createPubSocket()
        registry.bind(registryPub, registryRouter)
        discovery.connectRegistry(registryRouter)
        provider.attachDiscovery(discovery)
        provider.bind(serviceEndpoint)
        query.connect(registryRouter)
        val finalQuery = query

        SampleSupport.waitUntil("registry query sample") {
            try {
                finalQuery.topology().stream()
                    .anyMatch { entry -> channelName == entry.channelName() }
            } catch (ex: RuntimeException) {
                false
            }
        }

        println("[registry-query] service: \"sample\" -> snapshot: found")
    } finally {
        SampleSupport.closeQuietly(provider)
        SampleSupport.closeQuietly(query)
        SampleSupport.closeQuietly(discovery)
        SampleSupport.closeQuietly(registry)
        SampleSupport.closeQuietly(ctx)
    }
}
// --8<-- [end:doc]
