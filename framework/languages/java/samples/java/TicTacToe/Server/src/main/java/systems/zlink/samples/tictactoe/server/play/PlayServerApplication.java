package systems.zlink.samples.tictactoe.server.play;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.gamespots.handlers.TicTacToeGameCreatedHandler;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServer.class)
public final class PlayServerApplication {
    private PlayServerApplication() {
    }

    public static ConfigurableApplicationContext run(SampleSettings settings) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayServerApplication.class)
            .web(WebApplicationType.NONE)
            .initializers(context ->
                context.getBeanFactory().registerSingleton("sampleSettings", settings));
        builder.application().setKeepAlive(true);
        return builder.run();
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework(SampleSettings settings) {
        return PlayServer.configure(settings);
    }

    @Bean
    TicTacToeGameCreatedHandler ticTacToeGameCreatedHandler() {
        return new TicTacToeGameCreatedHandler();
    }
}
