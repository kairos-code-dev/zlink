package systems.zlink.e2e.kotlin.discoveryregistryha.registry

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.discoveryregistryha.RegistryOptions
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.registry.ZLinkRegistryQuery

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.discoveryregistryha.registry"],
)
class RegistryApplication {
    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(RegistryApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun registryRoleOptions(args: ApplicationArguments): RegistryOptions =
        RegistryOptions.parse(args.sourceArgs)

    @Bean
    fun registryOptions(roleOptions: RegistryOptions): ZLinkEmbeddedRegistryOptions {
        val options = ZLinkEmbeddedRegistryOptions()
        options.setRegistryId(roleOptions.registryId())
        options.setPubEndpoint(roleOptions.pubEndpoint())
        options.setRouterEndpoint(roleOptions.routerEndpoint())
        for (peer in roleOptions.peers()) {
            options.addPeer(peer)
        }
        return options
    }

    @Bean
    fun registryProbeServer(
        query: ZLinkRegistryQuery,
        json: ObjectMapper,
        roleOptions: RegistryOptions,
    ): RegistryProbeServer =
        RegistryProbeServer(query, json, roleOptions.httpEndpoint())
}
