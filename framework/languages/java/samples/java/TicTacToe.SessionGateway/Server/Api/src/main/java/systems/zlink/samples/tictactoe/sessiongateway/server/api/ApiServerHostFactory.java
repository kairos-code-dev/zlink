package systems.zlink.samples.tictactoe.sessiongateway.server.api;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = ApiServer.class)
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
            options.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            ApiServer.configure(options);
        };
    }
}
