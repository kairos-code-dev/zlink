package systems.zlink.samples.tictactoe.server.play;

import java.nio.file.Path;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.core.env.StandardEnvironment;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.samples.tictactoe.server.configuration.SampleLocationStore;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.application.gamecreation.TicTacToeGameCreator;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.handlers.TicTacToeGameCreatedHandler;



@EnableZLinkFramework
@EnableConfigurationProperties(SampleSettings.class)
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServer.class)
public final class PlayServerApplication {
    private PlayServerApplication() {
    }

    public static ConfigurableApplicationContext run(String configPath) {
        StandardEnvironment environment = new StandardEnvironment();
        environment.getPropertySources().remove(StandardEnvironment.SYSTEM_ENVIRONMENT_PROPERTY_SOURCE_NAME);
        environment.getPropertySources().remove(StandardEnvironment.SYSTEM_PROPERTIES_PROPERTY_SOURCE_NAME);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayServerApplication.class)
            .environment(environment)
            .web(WebApplicationType.NONE)
            .properties("spring.config.location=" + Path.of(configPath).toAbsolutePath().toUri());
        builder.application().setKeepAlive(true);
        return builder.run(new String[0]);
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework(SampleSettings settings) {
        return PlayServer.configure(settings);
    }

    @Bean
    TicTacToeGameCreatedHandler ticTacToeGameCreatedHandler() {
        return new TicTacToeGameCreatedHandler();
    }

    @Bean
    TicTacToeGameCreator ticTacToeGameCreator(
        SampleSettings settings) {
        return new TicTacToeGameCreator(settings);
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore(SampleSettings settings) {
        return SampleLocationStore.create(settings);
    }

    @Bean
    ObjectMapper ticTacToeJsonMapper() {
        return JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build();
    }
}
