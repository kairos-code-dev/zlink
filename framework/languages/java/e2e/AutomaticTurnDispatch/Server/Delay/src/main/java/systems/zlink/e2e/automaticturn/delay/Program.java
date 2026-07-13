package systems.zlink.e2e.automaticturn.delay;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.automaticturn.shared.Contracts;
import systems.zlink.e2e.automaticturn.shared.DelayHandler;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.automaticturn.delay")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run(args);
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/delay-flow.log")
                .traceLabel("java-atd-delay");
            options.addClientServerChannel(Contracts.DELAY_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_DELAY_ENDPOINT"))
                .setRoutingId(RoutingId.from("delay-a"))
                .addRequestHandler(
                    DelayHandler.class,
                    Contracts.DelayReq.class,
                    Contracts.DelayRes.class,
                    "DelayReq");
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")));
    }

    @Bean
    DelayHandler delayHandler() {
        return new DelayHandler();
    }
}
