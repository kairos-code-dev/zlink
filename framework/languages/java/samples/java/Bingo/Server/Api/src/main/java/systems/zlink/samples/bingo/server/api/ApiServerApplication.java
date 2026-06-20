package systems.zlink.samples.bingo.server.api;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = ApiServerApplication.class)
public final class ApiServerApplication {
    private ApiServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ApiServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer apiFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.codecs().addJson();
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addHandlersFromPackageOf(ApiServerApplication.class);
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(SampleTopology.selectedApiChannelEndpoint())
                .addHandlerGroup("api");
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(SampleNames.PlayChannel);
            route.enableClient(SampleTopology.PlayARouteEndpoint);
            route.enableClient(SampleTopology.PlayBRouteEndpoint);
            route.configureRouting().setRoutingId(RoutingId.from(SampleTopology.selectedApiRouteRid()));
        };
    }
}
