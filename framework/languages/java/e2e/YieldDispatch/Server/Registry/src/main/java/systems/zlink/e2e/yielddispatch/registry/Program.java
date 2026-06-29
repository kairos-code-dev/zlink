package systems.zlink.e2e.yielddispatch.registry;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.yielddispatch.shared.Env;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;

@SpringBootApplication(proxyBeanMethods = false)
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
    ZLinkEmbeddedRegistryOptions registryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_PUB"));
        options.setRouterEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
        return options;
    }
}
