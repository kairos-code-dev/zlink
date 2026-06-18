package systems.zlink.samples.supportchat.server.api;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTopology;



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
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addHandlersFromPackageOf(ApiServerApplication.class);
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(SampleTopology.ApiChannelEndpoint)
                .addHandlerGroup("api");
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableClient();
        };
    }
}
