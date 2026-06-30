package systems.zlink.e2e.kotlin.runtimemonitoring.registry

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.Env
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer
import java.time.Duration

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.runtimemonitoring.registry"],
)
class RegistryApplication {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceState(): EvidenceState = EvidenceState()

    @Bean
    fun evidenceHttpServer(state: EvidenceState, json: ObjectMapper): EvidenceHttpServer {
        return EvidenceHttpServer(state, json, Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))
    }

    @Bean
    fun registryOptions(): ZLinkEmbeddedRegistryOptions {
        val options = ZLinkEmbeddedRegistryOptions()
        options.setPubEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_PUB"))
        options.setRouterEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
        return options
    }

    @Bean
    fun monitoringOptions(): ZLinkMonitoringOptionsCustomizer {
        return ZLinkMonitoringOptionsCustomizer { options ->
            options.addRegistryEvents(
                Contracts.REGISTRY_SOURCE,
                Duration.ofMillis(100),
            )
        }
    }

    @Bean
    fun registryRecorder(state: EvidenceState): RegistryEventRecorder {
        return RegistryEventRecorder(state)
    }

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
}
