package systems.zlink.samples.bingo.server.api;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = ApiServerHostFactory.class)
public final class ApiServerHostFactory {
    private ApiServerHostFactory() {
    }

    public static AutoCloseable start(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ApiServerHostFactory.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkOptionsCustomizer apiOptions() {
        return options -> {
            options.useDiscovery(discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint));
            options.codecs().addJson();
            options.addHandlersFromPackageOf(ApiServerHostFactory.class);
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
                channel.enableServer(server -> server.bind(SampleTopology.ApiChannelEndpoint));
                channel.addHandlerGroup("api");
            });
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> channel.enableClient());
        };
    }
}
