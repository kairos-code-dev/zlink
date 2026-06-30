package systems.zlink.e2e.registrymessaging.registry;

import java.util.Map;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.registrymessaging.registry.Configuration.RegistryOptions;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrymessaging.registry")
public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.SERVLET)
            .properties(Map.of("server.port", RegistryOptions.get("ZLINK_JAVA_E2E_HTTP_PORT")));
        builder.application().setKeepAlive(true);
        builder.run(args);
    }

    @Bean
    ZLinkEmbeddedRegistryOptions registryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(RegistryOptions.get("ZLINK_JAVA_E2E_REGISTRY_PUB"));
        options.setRouterEndpoint(RegistryOptions.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
        return options;
    }
}
