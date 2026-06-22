package systems.zlink.samples.deliverydispatch.server.dispatchcenter;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = DispatchCenterApplication.class)
public final class DispatchCenterApplication {
    private DispatchCenterApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(DispatchCenterApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    DispatchWorkQueue dispatchWorkQueue() {
        return new DispatchWorkQueue();
    }

    @Bean
    ZLinkFrameworkConfigurer dispatchCenterFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.codecs().addJson();
            options.addHandlersFromPackageOf(DispatchCenterApplication.class);
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableServer(SampleTopology.DispatchChannelEndpoint)
                .addHandlerGroup("dispatch");
            options.addClientServerChannel(SampleNames.CourierAChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.CourierBChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient();
        };
    }
}
