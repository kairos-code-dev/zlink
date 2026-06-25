package systems.zlink.e2e.kotlin.monitoring;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.kotlin.monitoring.registry")
public final class RegistryApplication {
    private RegistryApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(RegistryApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceState evidenceState() {
        return new EvidenceState();
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(EvidenceState state, ObjectMapper json) {
        return new EvidenceHttpServer(state, json, Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkEmbeddedRegistryOptions registryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_PUB"));
        options.setRouterEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"));
        return options;
    }

    @Bean
    ZLinkMonitoringOptionsCustomizer monitoringOptions() {
        return options -> options.addRegistryEvents(
            Contracts.REGISTRY_SOURCE,
            Duration.ofMillis(100));
    }

    @Bean
    MonitoringEventHandlers.RegistryRecorder registryRecorder(EvidenceState state) {
        return new MonitoringEventHandlers.RegistryRecorder(state);
    }
}
