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
            publisher.publishSpot(
                    Contracts.SPOT_MESH,
                    "spot.events",
                    new Contracts.MeshEvent("c4-publisher"))
                .packetName("MeshEvent")
                .await();
            System.out.println("scenario SM-C4 passed");
        } finally {
            context.close();
        }
    }

    @Bean
    systems.zlink.framework.spring.ZLinkFrameworkConfigurer publisherFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/publisher-flow.log")
                .traceLabel("java-sm-publisher");
            options.addSpotMesh(Contracts.SPOT_MESH)
                .enablePubSub(Env.get("ZLINK_JAVA_E2E_SPOT_PUBLISHER_ENDPOINT"))
                .setRoutingId(RoutingId.from("publisher"));
        };
    }
}
