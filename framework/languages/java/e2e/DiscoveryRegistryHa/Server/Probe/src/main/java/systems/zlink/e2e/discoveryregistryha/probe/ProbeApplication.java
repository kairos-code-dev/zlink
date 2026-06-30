package systems.zlink.e2e.discoveryregistryha.probe;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient;

@SpringBootApplication(proxyBeanMethods = false)
public final class ProbeApplication {
    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ProbeOptions probeOptions() {
        return ProbeOptions.fromEnv();
    }

    @Bean
    ZLinkRegistryQueryClient registryQueryClient(
        ProbeOptions options,
        ZLinkBackendAdapterFactory backendAdapterFactory) {
        return ZLinkRemoteRegistryQueryClient.connect(
            options.queryRegistryRouter(),
            backendAdapterFactory);
    }

    @Bean
    ProbeEndpoints probeEndpoints(
        ProbeOptions options,
        ZLinkRegistryQueryClient query,
        ObjectMapper json) {
        return new ProbeEndpoints(query, json, options.httpEndpoint());
    }
}
