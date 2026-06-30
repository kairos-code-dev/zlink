package systems.zlink.e2e.kotlin.yielddispatch;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.kotlin.yielddispatch.delay")
public final class DelayApplication {
    private DelayApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(DelayApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            String nodeRid = Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "delay-a");
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"));
            ClientServerChannelBuilder delay = options.addClientServerChannel(Contracts.DELAY_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_DELAY_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            delay.addRequestHandler(
                DelayHandler.class,
                Contracts.DelayReq.class,
                Contracts.DelayRes.class,
                "DelayReq");
        };
    }
}
