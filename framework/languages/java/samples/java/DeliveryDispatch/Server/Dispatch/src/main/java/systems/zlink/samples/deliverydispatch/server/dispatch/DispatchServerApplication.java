package systems.zlink.samples.deliverydispatch.server.dispatch;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleLocationStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = DispatchServerApplication.class)
public final class DispatchServerApplication {
    private DispatchServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(DispatchServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer dispatchFramework() {
        return options -> {
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs")
                    + "/flow-dispatch.log")
                .traceLabel("dispatch");
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient();
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean
    DispatchWorkQueue dispatchWorkQueue(DispatchWorker worker) {
        return new DispatchWorkQueue(worker);
    }

    @Bean
    DispatchWorker dispatchWorker(systems.zlink.framework.channels.ZLinkClient channels) {
        return new DispatchWorker(channels);
    }

    @Bean
    DispatchHttpServer dispatchHttpServer(
        ObjectMapper json,
        DispatchWorkQueue queue) throws IOException {
        return new DispatchHttpServer(json, queue);
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }
}
