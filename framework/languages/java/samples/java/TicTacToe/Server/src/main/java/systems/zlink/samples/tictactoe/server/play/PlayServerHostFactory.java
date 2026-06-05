package systems.zlink.samples.tictactoe.server.play;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServer.class)
public final class PlayServerHostFactory {
    private PlayServerHostFactory() {
    }

    public static ConfigurableApplicationContext start(SampleSettings settings) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayServerHostFactory.class)
            .web(WebApplicationType.NONE)
            .initializers(context ->
                context.getBeanFactory().registerSingleton("sampleSettings", settings));
        builder.application().setKeepAlive(true);
        return builder.run();
    }

    @Bean
    ZLinkFrameworkOptionsCustomizer playOptions(SampleSettings settings) {
        return PlayServer.configure(settings);
    }
}
