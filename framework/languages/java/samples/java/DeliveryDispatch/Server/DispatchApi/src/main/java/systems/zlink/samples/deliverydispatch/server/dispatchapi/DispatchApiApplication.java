package systems.zlink.samples.deliverydispatch.server.dispatchapi;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = DispatchApiApplication.class)
public final class DispatchApiApplication {
    private DispatchApiApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(DispatchApiApplication.class)
            .web(WebApplicationType.NONE);
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
            options.codecs().addJson();
            options.addHandlersFromPackageOf(DispatchApiApplication.class);
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(SampleTopology.ApiChannelEndpoint)
                .addHandlerGroup("api");
            options.addClientServerChannel(SampleNames.DispatchChannel)
                .enableClient();
        };
    }
}
