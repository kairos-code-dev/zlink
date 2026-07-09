package systems.zlink.samples.supportchat.server.session;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.supportchat.server.configuration.SampleFlowLog;
import systems.zlink.samples.supportchat.server.configuration.SampleLocationStore;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTopology;
import systems.zlink.samples.supportchat.server.session.sessions.SupportChatSession;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false, scanBasePackageClasses = Program.class)
public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        AutoCloseable app = run(args);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            try {
                app.close();
            } catch (Exception ignored) {
            }
        }));
        Thread.currentThread().join();
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer sessionFramework() {
        return options -> {
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(SampleFlowLog.path("session"))
                .traceLabel("session");
            options.configureLocations();
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableClient();
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.SupportActorMesh);
            node.enableRouter(SampleTopology.SessionSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.SessionSpotNodeRid));
            node.connectRouter(
                RoutingId.from(SampleTopology.SupportSpotNodeRid),
                SampleTopology.SupportSpotRouterEndpoint);
            options.addStreamNode(SampleNames.StreamNode)
                .bind(SampleTopology.SessionStreamEndpoint)
                .registerSession(SupportChatSession.class);
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }
}
