package systems.zlink.samples.tictactoe.server.api;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.tictactoe.server.api.handlers.AuthenticatePlayerHandler;
import systems.zlink.samples.tictactoe.server.api.handlers.CreateGameHttpHandler;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleLogging;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = {
        ApiServer.class,
        AuthenticatePlayerHandler.class,
        CreateGameHttpHandler.class
    })
public class ApiServer {
    public static ConfigurableApplicationContext start(SampleSettings settings) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ApiServer.class)
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
    ZLinkFrameworkOptionsCustomizer apiOptions(SampleSettings settings) {
        return options -> {
            SampleLogging.configure(settings, "api");
            options.codecs().addJson();
            options.addHandlersFromPackageOf(ApiServer.class);
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> {
                channel.enableServer(server -> server.bind(settings.apiChannelEndpoint()));
                channel.addHandlerGroup("api");
            });
            options.addClientServerChannel(SampleNames.PlayChannel, channel ->
                channel.enableClient(client -> client.useManualConnections(
                    endpoints -> endpoints.connect(settings.playChannelEndpoint()))));
        };
    }
}
