package systems.zlink.e2e.runtimemonitoring.registry;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.runtimemonitoring.registry.support.EvidenceHttpServer;
import systems.zlink.e2e.runtimemonitoring.registry.support.EvidenceState;
import systems.zlink.e2e.runtimemonitoring.registry.support.MonitoringEventHandlers;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.e2e.runtimemonitoring.shared.Env;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.runtimemonitoring.registry")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run(args);
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
        return new EvidenceHttpServer(state, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkEmbeddedRegistryOptions registryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_PUB"));
        options.setRouterEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
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
