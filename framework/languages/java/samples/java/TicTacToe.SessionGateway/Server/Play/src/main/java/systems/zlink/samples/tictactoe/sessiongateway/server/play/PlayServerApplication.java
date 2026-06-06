package systems.zlink.samples.tictactoe.sessiongateway.server.play;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServer.class)
public final class PlayServerApplication {
    private PlayServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework() {
        return options -> {
            options.useDiscovery(discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint));
            PlayServer.configure(options);
        };
    }

    @Bean
    TicTacToeGameDirectory ticTacToeGameDirectory() {
        return new TicTacToeGameDirectory();
    }
}
