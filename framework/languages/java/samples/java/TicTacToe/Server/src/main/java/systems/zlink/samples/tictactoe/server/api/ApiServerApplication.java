package systems.zlink.samples.tictactoe.server.api;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.api.handlers.AuthenticatePlayerHandler;
import systems.zlink.samples.tictactoe.server.api.handlers.CreateGameHttpHandler;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = {
        ApiServer.class,
        AuthenticatePlayerHandler.class,
        CreateGameHttpHandler.class
    })
public final class ApiServerApplication {
    private ApiServerApplication() {
    }

    public static ConfigurableApplicationContext run(SampleSettings settings) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ApiServerApplication.class)
            .web(WebApplicationType.SERVLET)
            .properties(
                "server.address=127.0.0.1",
                "server.port=" + settings.apiHttpPort())
            .initializers(context ->
                context.getBeanFactory().registerSingleton("sampleSettings", settings));
        builder.application().setKeepAlive(true);
        return builder.run();
    }

    @Bean
    ZLinkFrameworkConfigurer apiFramework(SampleSettings settings) {
        return ApiServer.configure(settings);
    }
}
