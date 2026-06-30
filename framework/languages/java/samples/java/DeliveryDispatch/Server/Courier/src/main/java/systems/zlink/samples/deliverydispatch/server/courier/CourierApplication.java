package systems.zlink.samples.deliverydispatch.server.courier;

import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.server.courier.handlers.OfferDeliveryHandler;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = CourierApplication.class)
public final class CourierApplication {
    private CourierApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(CourierApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    CourierOptions courierOptions(ApplicationArguments arguments) {
        return CourierOptions.fromArgs(arguments.getSourceArgs());
    }

    @Bean
    ZLinkFrameworkConfigurer courierFramework(CourierOptions options) {
        return configurer -> {
            configurer.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            configurer.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs") + "/flow-" + options.courierId() + ".log")
                .traceLabel(options.courierId());
            configurer.addClientServerChannel(SampleNames.courierChannel(options.courierId()))
                .enableServer(SampleTopology.courierEndpoint(options.courierId()))
                .addRequestHandler(
                    OfferDeliveryHandler.class,
                    Messages.OfferDelivery.class,
                    Messages.OfferDeliveryRes.class);
        };
    }
}
