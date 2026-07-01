package systems.zlink.samples.deliverydispatch.server.tracking;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = TrackingServerApplication.class)
public final class TrackingServerApplication {
    private TrackingServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(TrackingServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer trackingFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs")
                    + "/flow-tracking.log")
                .traceLabel("tracking");
            options.addHandlersFromPackageOf(TrackingServerApplication.class);
            options.addClientServerChannel(SampleNames.CustomerRouteChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableServer(SampleTopology.TrackingChannelEndpoint)
                .addHandlerGroup("tracking");
        };
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }
}
