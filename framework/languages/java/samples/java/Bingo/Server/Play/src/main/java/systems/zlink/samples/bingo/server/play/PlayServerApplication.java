package systems.zlink.samples.bingo.server.play;

import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.json.JsonMapper;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMeshNodeBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActorFactory;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActorTransferAdapter;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.matchmaking.RedisBingoMatchQueue;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoRoomSettingsInitializer;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoMatchQueue;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocator;
import systems.zlink.samples.bingo.server.configuration.SampleLocationStore;
import systems.zlink.samples.bingo.server.configuration.SampleApplication;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.server.configuration.BingoMetricsReporter;
import io.micrometer.core.instrument.MeterRegistry;



@EnableZLinkFramework
@EnableConfigurationProperties(SampleTopology.class)
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = PlayServerApplication.class)
public final class PlayServerApplication {
    private PlayServerApplication() {
    }

    public static AutoCloseable run(String configPath) {
        return SampleApplication.start(PlayServerApplication.class, configPath)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework(SampleTopology topology) {
        return options -> {
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(topology.logDirectory() + "/flow-play.log")
                .traceLabel("play");
            options.codecs().use(ZLinkProtobufCodec.defaultCodec());
            options.configureLocations();
            options.addHandlersFromPackageOf(PlayServerApplication.class);
            ZLinkMeshNodeBuilder node = options.addRouteMesh(SampleNames.Mesh);
            node.listen(topology.selectedPlaySpotRouterEndpoint())
                .useAllocatedRoutingId(2, "play")
                .setRoutingIdAllocationGroup(SampleNames.PlayAllocationGroup);
            node.channelName(SampleNames.ApiChannel).setWeight(0);
            node.channelName(SampleNames.RoomSpotDiscovery);
            node.addEntrySpot(BingoEntrySpot.class);
            node.addSpotFactory(BingoRoomSpot.class);
            node.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
            node.addActorTransferAdapter(
                SampleNames.PlayerActorType,
                PlayerActorTransferAdapter.class);
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore(SampleTopology topology) {
        return SampleLocationStore.create(topology);
    }

    @Bean
    BingoRoomAllocator bingoRoomAllocator(BingoMatchQueue matchQueue) {
        return new BingoRoomAllocator(matchQueue, SampleTimings.DrawPeriod.toMillis());
    }

    @Bean
    BingoMatchQueue redisBingoMatchQueue(SampleTopology topology) {
        return new RedisBingoMatchQueue(topology);
    }

    @Bean
    BingoRoomSettingsInitializer bingoRoomSettingsInitializer() {
        return new BingoRoomSettingsInitializer();
    }

    @Bean
    ObjectMapper bingoJsonMapper() {
        return JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build();
    }

    @Bean(destroyMethod = "close")
    BingoMetricsReporter bingoMetricsReporter(MeterRegistry registry) {
        return new BingoMetricsReporter(registry, "play");
    }
}
