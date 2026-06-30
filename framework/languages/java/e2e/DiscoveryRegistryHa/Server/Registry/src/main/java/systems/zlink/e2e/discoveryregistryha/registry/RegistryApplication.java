package systems.zlink.e2e.discoveryregistryha.registry;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryQuery;

@SpringBootApplication(proxyBeanMethods = false)
public final class RegistryApplication {
    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    RegistryOptions registryRoleOptions() {
        return RegistryOptions.fromEnv();
    }

    @Bean
    ZLinkEmbeddedRegistryOptions registryOptions(RegistryOptions roleOptions) {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setRegistryId(roleOptions.registryId());
        options.setPubEndpoint(roleOptions.pubEndpoint());
        options.setRouterEndpoint(roleOptions.routerEndpoint());
        options.setHeartbeatInterval(Duration.ofMillis(250));
        options.setHeartbeatTimeout(Duration.ofSeconds(2));
        options.setBroadcastInterval(Duration.ofMillis(250));
        for (String peer : roleOptions.peerEndpoints()) {
            options.addPeer(peer);
        }
        return options;
    }

    @Bean
    RegistryEndpoints registryEndpoints(
        RegistryOptions options,
        ZLinkRegistryQuery query,
        ObjectMapper json) {
        return new RegistryEndpoints(query, json, options.httpEndpoint());
    }
}
