package systems.zlink.e2e.kotlin.discoveryregistryha.probe

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.ApplicationArguments
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.discoveryregistryha.probe.Configuration.ProbeOptions
import systems.zlink.framework.registry.ZLinkRegistryQueryClient
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.discoveryregistryha.probe"],
)
class ProbeApplication {
    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ProbeApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun probeOptions(args: ApplicationArguments): ProbeOptions =
        ProbeOptions.parse(args.sourceArgs)

    @Bean
    fun registryQueryClient(
        backendAdapterFactory: ZLinkBackendAdapterFactory,
        probeOptions: ProbeOptions,
    ): ZLinkRegistryQueryClient =
        ZLinkRemoteRegistryQueryClient.connect(probeOptions.registryRouter, backendAdapterFactory)

    @Bean
    fun probeHttpServer(
        query: ZLinkRegistryQueryClient,
        json: ObjectMapper,
        probeOptions: ProbeOptions,
    ): ProbeHttpServer =
        ProbeHttpServer(query, json, probeOptions)
}
