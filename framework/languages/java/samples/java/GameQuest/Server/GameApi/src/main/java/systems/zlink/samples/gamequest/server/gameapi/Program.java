package systems.zlink.samples.gamequest.server.gameapi;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.samples.gamequest.server.configuration.SampleLocationStore;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.configuration.SampleTopology;
import systems.zlink.samples.gamequest.server.gameapi.sessions.GameQuestSession;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = Program.class)
public class Program {
    public static void main(String[] args) throws Exception {
        AutoCloseable app = run(args);
        GameQuestStore store = ApplicationContextHolder.store;
        HttpServer http = startHttp(store);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            http.stop(0);
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
    ZLinkFrameworkConfigurer gameApiFramework() {
        return options -> {
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(System.getenv().getOrDefault("GAMEQUEST_LOG_DIR", "logs")
                    + "/flow-" + SampleTopology.apiName() + ".log")
                .traceLabel(SampleTopology.apiName());
            options.addClientServerChannel(SampleNames.questOwnerChannelFor("mission-a"))
                .enableClient(SampleTopology.missionAOwnerChannelEndpoint());
            options.addClientServerChannel(SampleNames.questOwnerChannelFor("mission-b"))
                .enableClient(SampleTopology.missionBOwnerChannelEndpoint());
            options.addStreamNode(SampleNames.StreamNode)
                .bind(SampleTopology.selectedApiStreamEndpoint())
                .registerSession(GameQuestSession.class);
        };
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return SampleLocationStore.create();
    }

    @Bean(destroyMethod = "close")
    GameQuestStore gameQuestStore() {
        GameQuestStore store = new GameQuestStore();
        ApplicationContextHolder.store = store;
        return store;
    }

    @Bean
    GameQuestApiServices gameQuestApiServices(GameQuestStore store, ZLinkClient channels) {
        ApplicationContextHolder.store = store;
        ApplicationContextHolder.channels = channels;
        return new GameQuestApiServices();
    }

    private static HttpServer startHttp(GameQuestStore store) throws IOException {
        ObjectMapper json = new ObjectMapper();
        URI uri = URI.create(SampleTopology.selectedApiHttpEndpoint());
        HttpServer server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
        server.createContext("/health", exchange -> writeJson(exchange, json, 200, new Health("ok")));
        server.createContext("/internal/snapshot", exchange -> {
            Messages.GetGameplaySnapshotReq request = readJson(exchange, json, Messages.GetGameplaySnapshotReq.class);
            writeJson(exchange, json, 200, store.snapshot(request.playerId()));
        });
        server.createContext("/quest/progress/", exchange -> {
            String playerId = exchange.getRequestURI().getPath().substring("/quest/progress/".length());
            writeJson(exchange, json, 200, new Messages.GetQuestProgressRes(store.projection(playerId)));
        });
        server.createContext("/self-check/gameplay/kill-without-publish/", exchange -> {
            String playerId = exchange.getRequestURI().getPath()
                .substring("/self-check/gameplay/kill-without-publish/".length());
            store.addUnpublishedKill(playerId);
            writeJson(exchange, json, 200, new Accepted(true));
        });
        server.createContext("/self-check/projection/", exchange ->
            handleProjection(exchange, json, store).exceptionally(error -> {
                writeJsonUnchecked(exchange, json, 500, new ErrorBody(error.getMessage()));
                return null;
            }));
        server.createContext("/self-check/assert", exchange -> writeJson(exchange, json, 200, store.assertState()));
        server.start();
        return server;
    }

    private static java.util.concurrent.CompletionStage<Void> handleProjection(
        HttpExchange exchange,
        ObjectMapper json,
        GameQuestStore store) {
        String[] parts = exchange.getRequestURI().getPath().split("/");
        String playerId = parts.length >= 4 ? parts[3] : "";
        String questId = parts.length >= 5 ? parts[4] : "";
        String action = parts.length >= 6 ? parts[5] : "";
        if ("delete".equals(action)) {
            return ApplicationContextHolder.channels
                .requestToChannel(
                    SampleNames.questOwnerChannelFor(SampleTopology.ownerMissionName(playerId)),
                    new Messages.DeleteQuestProjectionReq(playerId, questId))
                .submit(Messages.DeleteQuestProjectionRes.class)
                .thenAccept(deleted -> {
                    store.deleteProjection(playerId, questId);
                    writeJsonUnchecked(exchange, json, 200, deleted);
                });
        }
        if ("rebuild".equals(action)) {
            return ApplicationContextHolder.channels
                .requestToChannel(
                    SampleNames.questOwnerChannelFor(SampleTopology.ownerMissionName(playerId)),
                    new Messages.RebuildQuestProjectionReq(playerId, questId, 0))
                .submit(Messages.QuestProgress.class)
                .thenAccept(rebuilt -> {
                    store.mergeProjection(playerId, java.util.List.of(rebuilt));
                    writeJsonUnchecked(exchange, json, 200, rebuilt);
                });
        }
        writeJsonUnchecked(exchange, json, 404, new ErrorBody("unknown projection action"));
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    private static void writeJsonUnchecked(
        HttpExchange exchange,
        ObjectMapper json,
        int status,
        Object body) {
        try {
            writeJson(exchange, json, status, body);
        } catch (IOException error) {
            throw new java.util.concurrent.CompletionException(error);
        }
    }

    private static <T> T readJson(HttpExchange exchange, ObjectMapper json, Class<T> type) throws IOException {
        return json.readValue(exchange.getRequestBody(), type);
    }

    private static void writeJson(HttpExchange exchange, ObjectMapper json, int status, Object body)
        throws IOException {
        byte[] bytes = json.writeValueAsString(body).getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().add("content-type", "application/json");
        exchange.sendResponseHeaders(status, bytes.length);
        exchange.getResponseBody().write(bytes);
        exchange.close();
    }

    private record Health(String status) {
    }

    private record Accepted(boolean accepted) {
    }

    private record Deleted(boolean deleted) {
    }

    private record ErrorBody(String error) {
    }

    private static final class GameQuestApiServices {
    }

    private static final class ApplicationContextHolder {
        private static GameQuestStore store;
        private static ZLinkClient channels;
    }
}
