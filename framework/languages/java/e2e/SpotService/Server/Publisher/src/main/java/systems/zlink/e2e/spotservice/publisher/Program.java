package systems.zlink.e2e.spotservice.publisher;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.spotservice.shared.Contracts;
import systems.zlink.e2e.spotservice.shared.Env;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotservice.publisher")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        ConfigurableApplicationContext context =
            new SpringApplicationBuilder(Program.class)
                .web(WebApplicationType.NONE)
                .run(args);
        try {
            ZLinkSpotPublisherClient publisher = context.getBean(ZLinkSpotPublisherClient.class);
            publisher.publish(
                    Contracts.SPOT_MESH,
                    "spot.events",
                    new Contracts.MeshMsg("c4-publisher"))
                .submit();
            System.out.println("scenario SM-C4 passed");
        } finally {
            context.close();
        }
    }

    @Bean
    systems.zlink.framework.spring.ZLinkFrameworkConfigurer publisherFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/publisher-flow.log")
                .traceLabel("java-sm-publisher");
            options.addSpotMesh(Contracts.SPOT_MESH)
                .enablePubSub(Env.get("ZLINK_JAVA_E2E_SPOT_PUBLISHER_ENDPOINT"))
                .setRoutingId(RoutingId.from("publisher"));
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")));
    }
}
