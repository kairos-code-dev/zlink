package systems.zlink.samples.deliverydispatch.server.dispatchapi;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.samples.deliverydispatch.server.dispatchapi.handlers.CreateDeliveryHandler;
import systems.zlink.samples.deliverydispatch.server.dispatchapi.handlers.ServerAssertionHandler;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = {
        DispatchApiApplication.class,
        CreateDeliveryHandler.class,
        ServerAssertionHandler.class
    })
public final class DispatchApiApplication {
    private DispatchApiApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(DispatchApiApplication.class)
            .web(WebApplicationType.SERVLET)
            .properties(
                "server.address=127.0.0.1",
                "server.port=" + httpPort(SampleTopology.ApiHttpUrl));
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }

    @Bean
    ZLinkFrameworkConfigurer dispatchApiFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs") + "/flow-dispatch-api.log")
                .traceLabel("dispatch-api");
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableClient();
        };
    }

    private static int httpPort(String url) {
        return java.net.URI.create(url).getPort();
    }
}
