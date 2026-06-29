package systems.zlink.e2e.discoveryregistryha;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryQuery;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.discoveryregistryha.registry")
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
    ZLinkEmbeddedRegistryOptions registryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setRegistryId(Integer.parseInt(Env.get("ZLINK_JAVA_E2E_REGISTRY_ID", "1")));
        options.setPubEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_PUB"));
        options.setRouterEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
        options.setHeartbeatInterval(Duration.ofMillis(250));
        options.setHeartbeatTimeout(Duration.ofSeconds(2));
        options.setBroadcastInterval(Duration.ofMillis(250));
        for (String peer : Env.csv("ZLINK_JAVA_E2E_REGISTRY_PEERS")) {
            options.addPeer(peer);
        }
        return options;
    }

    @Bean
    RegistryProbeServer registryProbeServer(
        ZLinkRegistryQuery query,
        ObjectMapper json) {
        return new RegistryProbeServer(query, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"));
    }
}
