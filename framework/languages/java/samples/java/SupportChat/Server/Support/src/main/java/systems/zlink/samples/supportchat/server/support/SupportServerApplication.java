package systems.zlink.samples.supportchat.server.support;

import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTopology;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportUserActorFactory;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.notifications.ConversationNotificationPublisher;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.ZLinkConversationStarter;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers.ConversationSpotCreatedHandler;
import systems.zlink.samples.supportchat.server.support.application.assignment.AgentAssignmentService;
import systems.zlink.samples.supportchat.server.support.application.assignment.AgentAvailabilityDirectory;
import systems.zlink.samples.supportchat.server.support.application.assignment.SupportConversationAllocator;



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = SupportServerApplication.class)
public final class SupportServerApplication {
    private SupportServerApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SupportServerApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer supportFramework() {
        return options -> {
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("SUPPORTCHAT_LOG_DIR", "logs") + "/flow-support.log")
                .traceLabel("support");
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.addHandlersFromPackageOf(SupportServerApplication.class);
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableServer(SampleTopology.SupportChannelEndpoint)
                .addHandlerGroup("support");
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient();
            RouteMeshChannelBuilder route = options.addRouteMesh(SampleNames.SupportRouteChannel);
            route.enableServer(SampleTopology.SupportRouteEndpoint);
            route.enableClient();
            route.setRoutingId(RoutingId.from(SampleTopology.SupportRid));
            options.useRegistrySpotRemoteAddresses(SampleNames.SupportSpotDiscovery)
                .setRouterChannelId(SampleNames.SupportRouteChannel);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(SampleNames.SupportSpotDiscovery)
                ;
            node.enableRouter(SampleTopology.SupportSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.SupportRid));
            node.enablePubSub(SampleTopology.SupportSpotEndpoint);node.addEntrySpot(SupportEntrySpot.class);
            node.addSpotFactory(ConversationSpot.class);
            node.addActorFactory(SampleNames.SupportActorType, SupportUserActorFactory.class);
        };
    }

    @Bean
    SupportConversationAllocator supportConversationAllocator(
        ObjectProvider<ZLinkSpotManager> spots,
        ObjectMapper json) {
        return new SupportConversationAllocator(new ZLinkConversationStarter(spots.getObject(), json));
    }

    @Bean
    ConversationNotificationPublisher conversationNotificationPublisher() {
        return new ConversationNotificationPublisher();
    }

    @Bean
    ConversationSpotCreatedHandler conversationSpotCreatedHandler(ObjectMapper json) {
        return new ConversationSpotCreatedHandler(json);
    }

    @Bean
    AgentAvailabilityDirectory agentAvailabilityDirectory() {
        return new AgentAvailabilityDirectory();
    }

    @Bean
    AgentAssignmentService agentAssignmentService(AgentAvailabilityDirectory availability) {
        return new AgentAssignmentService(availability);
    }

    @Bean
    SupportActorDirectory supportActorDirectory() {
        return new SupportActorDirectory();
    }

    @Bean
    ObjectMapper supportChatJsonMapper() {
        return JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build();
    }
}
