package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;

final class SampleReleaseGateContractTest {
    private static final Set<String> REQUIRED_LANGUAGES = Set.of("java", "kotlin");

    private static final Set<String> REQUIRED_SAMPLES = Set.of(
        "TicTacToe",
        "TicTacToe.SessionGateway",
        "Bingo",
        "StreamingClient",
        "Async");

    private static final Set<String> REQUIRED_RUNTIME_PACKAGES = Set.of(
        "actors",
        "backend",
        "binding",
        "channels",
        "configuration",
        "host",
        "messaging",
        "monitoring",
        "registry",
        "spots",
        "streams");

    private static final List<String> FORBIDDEN_SAMPLE_PATTERNS = List.of(
        "import systems.zlink.runtime.",
        "import systems.zlink.internal.",
        "RouteStore",
        "MetadataStore",
        "RemoteAddressResolver",
        "Thread.sleep",
        "sleep(",
        "toCompletableFuture()",
        "session relay JSON",
        "in-memory route channel replacement");

    @Test
    void requiredSamplesExposeExecutableEntryPoints() {
        Path samplesRoot = samplesRoot();

        assertTrue(Files.isRegularFile(samplesRoot.resolve("run_samples.sh")),
            "missing aggregate sample runner");
        assertTrue(Files.isExecutable(samplesRoot.resolve("run_samples.sh")),
            "aggregate sample runner must be executable");

        for (String language : REQUIRED_LANGUAGES) {
            Path languageRoot = samplesRoot.resolve(language);
            assertTrue(Files.isDirectory(languageRoot), "missing sample language directory " + language);

            for (String sample : REQUIRED_SAMPLES) {
                Path sampleRoot = languageRoot.resolve(sample);
                String sampleName = language + "/" + sample;
                assertTrue(Files.isDirectory(sampleRoot), "missing sample " + sampleName);
                assertTrue(Files.isRegularFile(sampleRoot.resolve("settings.gradle.kts")),
                    "missing settings.gradle.kts for " + sampleName);
                assertTrue(Files.isRegularFile(sampleRoot.resolve("build.gradle.kts")),
                    "missing build.gradle.kts for " + sampleName);
                assertTrue(Files.isRegularFile(sampleRoot.resolve("run_sample.sh")),
                    "missing run_sample.sh for " + sampleName);
                assertTrue(Files.isExecutable(sampleRoot.resolve("run_sample.sh")),
                    "run_sample.sh must be executable for " + sampleName);
            }
        }
    }

    @Test
    void sampleSourcesUseOnlyPublicFrameworkAndConnectorApi() throws IOException {
        try (Stream<Path> files = Files.walk(samplesRoot())) {
            Map<Path, List<String>> offenders = files
                .filter(Files::isRegularFile)
                .filter(SampleReleaseGateContractTest::isSampleSource)
                .map(path -> Map.entry(path, forbiddenLines(path)))
                .filter(entry -> !entry.getValue().isEmpty())
                .collect(java.util.stream.Collectors.toMap(
                    Map.Entry::getKey,
                    Map.Entry::getValue));

            assertTrue(offenders.isEmpty(), "sample forbidden pattern offenders: " + offenders);
        }
    }

    @Test
    void frameworkRuntimeSourcesStaySplitByDotNetRuntimeCategories() throws IOException {
        Path runtimeRoot = frameworkJavaRoot()
            .resolve("zlink-framework-core/src/main/java/systems/zlink/framework/runtime");

        for (String runtimePackage : REQUIRED_RUNTIME_PACKAGES) {
            assertTrue(Files.isDirectory(runtimeRoot.resolve(runtimePackage)),
                "missing runtime package " + runtimePackage);
        }

        try (Stream<Path> files = Files.list(runtimeRoot)) {
            List<Path> rootJavaFiles = files
                .filter(Files::isRegularFile)
                .filter(path -> path.getFileName().toString().endsWith(".java"))
                .filter(path -> !path.getFileName().toString().equals("package-info.java"))
                .toList();

            assertTrue(rootJavaFiles.isEmpty(),
                "runtime root must not contain implementation files: " + rootJavaFiles);
        }
    }

    @Test
    void ticTacToeSessionGatewayUsesActorGatewayAndFrameworkActorLocator() throws IOException {
        assertSampleFilesExist("java", "TicTacToe.SessionGateway", "src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/TicTacToeSessionGatewaySample.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchClient.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchClientOptions.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorNotificationInbox.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/ApiServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/handlers/AuthenticateActorHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/handlers/CreateMatchHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/PlayServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/handlers/CreateMatchRoomHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/handlers/EnsurePlayerActorHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameSpot.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameContractMapper.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameModels.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/GameNotificationPublisher.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/handlers/PlaceMarkHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/handlers/TicTacToeGameSpotActorJoinedHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/handlers/TicTacToeGameSpotActorLeftHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/handlers/TicTacToeGameSpotCreatedHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/entryspot/TicTacToeEntrySpot.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/entryspot/handlers/JoinMatchHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorJoinedHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorLeftHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/registry/RegistryServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/SessionServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/PlayerSession.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/AuthenticateSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/CreateMatchSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/actors/PlayerActor.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/actors/PlayerActorFactory.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/configuration/SampleNames.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/configuration/SampleTopology.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/contracts/Messages.java"));

        String sessionServerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/SessionServer.java");
        String playServerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/PlayServer.java");
        String clientSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchClient.java");
        String playerClientSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.java");
        String mainSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/TicTacToeSessionGatewaySample.java");

        assertTrue(sessionServerSource.contains("attachActorGateway(SampleNames.SessionRelayNode)")
                || sessionServerSource.contains("attachActorGateway(\"session-relay\")"),
            "SessionGateway sample must attach stream node to local ActorGateway SpotNode");
        assertTrue(mainSource.contains("ZLinkFramework.start"),
            "SessionGateway sample must start the framework through the public facade");
        assertTrue(playServerSource.contains("useRegistrySpotRemoteAddresses"),
            "SessionGateway sample must use registry-backed Spot remote addresses");
        assertFalse(mainSource.contains("RecordingStreamNodeBuilder"),
            "SessionGateway sample must not replace stream node configuration with a recording builder");
        assertTrue(clientSource.contains("new ZLinkActorRef("),
            "SessionGateway sample must bind by framework actor locator");
        assertTrue(playerClientSource.contains("context().actors().bindAsync"),
            "SessionGateway sample must bind actors through ZLinkSessionContext public API");
        assertFalse(clientSource.contains("systems.zlink.contracts.service.spot.ActorRef"),
            "SessionGateway sample must not import binding ActorRef");
        assertFalse(clientSource.contains("new ActorRef("),
            "SessionGateway sample must not construct binding ActorRef");
        assertFalse(clientSource.contains("ZLinkSpotRemoteAddressResolver"),
            "session handler must not call actor remote address resolver");
    }

    @Test
    void ticTacToeSessionGatewayKotlinSampleMirrorsJavaRoleLayout() {
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/TicTacToeSessionGatewayKotlinSample.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchClient.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchClientOptions.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorNotificationInbox.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/ApiServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/handlers/AuthenticateActorHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/handlers/CreateMatchHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/PlayServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/handlers/CreateMatchRoomHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/handlers/EnsurePlayerActorHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameSpot.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameContractMapper.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameModels.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/GameNotificationPublisher.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/handlers/PlaceMarkHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/handlers/TicTacToeGameSpotActorJoinedHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/handlers/TicTacToeGameSpotActorLeftHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/handlers/TicTacToeGameSpotCreatedHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/entryspot/TicTacToeEntrySpot.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/entryspot/handlers/JoinMatchHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorJoinedHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorLeftHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/registry/RegistryServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/SessionServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/PlayerSession.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/AuthenticateSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/CreateMatchSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/actors/PlayerActor.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/actors/PlayerActorFactory.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/configuration/SampleTopology.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/contracts/Messages.kt"));
    }

    @Test
    void ticTacToeDirectSampleUsesFrameworkRuntimePublicFacade() throws IOException {
        assertSampleFilesExist("java", "TicTacToe", "src/main/java", List.of(
            "systems/zlink/samples/tictactoe/TicTacToeSample.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClient.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClientOptions.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClientResult.java",
            "systems/zlink/samples/tictactoe/server/api/ApiServer.java",
            "systems/zlink/samples/tictactoe/server/api/handlers/AuthenticatePlayerHandler.java",
            "systems/zlink/samples/tictactoe/server/api/handlers/CreateGameHttpHandler.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleNames.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleTopology.java",
            "systems/zlink/samples/tictactoe/server/play/PlayServer.java",
            "systems/zlink/samples/tictactoe/server/play/actors/PlayActor.java",
            "systems/zlink/samples/tictactoe/server/play/actors/PlayActorFactory.java",
            "systems/zlink/samples/tictactoe/server/play/entryspot/PlayEntrySpot.java",
            "systems/zlink/samples/tictactoe/server/play/entryspot/handlers/PlayActorJoinGameHandler.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/TicTacToeGame.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/PlayActorPlaceMarkHandler.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameJoinHandler.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameTimerHandler.java",
            "systems/zlink/samples/tictactoe/server/play/handlers/CreateGameHandler.java",
            "systems/zlink/samples/tictactoe/server/play/sessions/PlaySession.java",
            "systems/zlink/samples/tictactoe/shared/contracts/GameState.java",
            "systems/zlink/samples/tictactoe/shared/contracts/CreateGameRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/JoinGameRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/PlaceMarkRes.java"));

        String mainSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/TicTacToeSample.java");
        String clientSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/client/TicTacToeClient.java");
        String apiSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/server/api/ApiServer.java");
        String playSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/server/play/PlayServer.java");

        assertTrue(mainSource.contains("ZLinkFramework.start"),
            "TicTacToe direct sample must start the framework through the public facade");
        assertTrue(clientSource.contains(".requestToChannel("),
            "TicTacToe direct sample must use framework channel request path");
        assertTrue(apiSource.contains(".addClientServerChannel("),
            "TicTacToe direct sample must expose the Api server role");
        assertTrue(playSource.contains(".addSpotMesh("),
            "TicTacToe direct sample must expose the Play Spot role");
        assertTrue(playSource.contains(".addStreamNode("),
            "TicTacToe direct sample must register the STREAM entry point");
        assertFalse(mainSource.contains("CreateGameHandler"),
            "TicTacToe main must not collapse Play handler wiring into the entry point");
    }

    @Test
    void ticTacToeKotlinSampleMirrorsJavaRoleLayout() {
        assertSampleFilesExist("kotlin", "TicTacToe", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/TicTacToeKotlinSample.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClient.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientOptions.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientResult.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/AuthenticatePlayerHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/CreateGameHttpHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleTopology.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/PlayServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/actors/PlayActor.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/actors/PlayActorFactory.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/entryspot/PlayEntrySpot.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/entryspot/handlers/PlayActorJoinGameHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/TicTacToeGame.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/PlayActorPlaceMarkHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameJoinHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameTimerHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/handlers/CreateGameHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/sessions/PlaySession.kt",
            "systems/zlink/samples/kotlin/tictactoe/shared/contracts/Contracts.kt"));
    }

    @Test
    void bingoMirrorsFourClientMatchingTimerAndBoundPushGate() throws IOException {
        assertSampleFilesExist("java", "Bingo", "src/main/java", List.of(
            "systems/zlink/samples/bingo/BingoSample.java",
            "systems/zlink/samples/bingo/client/BingoClientApp.java",
            "systems/zlink/samples/bingo/client/BingoClientOptions.java",
            "systems/zlink/samples/bingo/client/BingoNotificationInbox.java",
            "systems/zlink/samples/bingo/client/BingoPlayerClient.java",
            "systems/zlink/samples/bingo/server/api/ApiServerHostFactory.java",
            "systems/zlink/samples/bingo/server/api/handlers/AuthenticatePlayerHandler.java",
            "systems/zlink/samples/bingo/server/api/handlers/MatchBingoHandler.java",
            "systems/zlink/samples/bingo/server/play/PlayServerHostFactory.java",
            "systems/zlink/samples/bingo/server/play/actors/PlayerActor.java",
            "systems/zlink/samples/bingo/server/play/actors/PlayerActorFactory.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoCard.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoNotificationPublisher.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoRoomModels.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoRoomSpot.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/handlers/BingoRoomJoinHandler.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/handlers/BingoRoomActorJoinedHandler.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/handlers/BingoRoomActorLeftHandler.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/handlers/BingoRoomSpotCreatedHandler.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/handlers/BingoRoomTimerHandler.java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/handlers/StartBingoGameHandler.java",
            "systems/zlink/samples/bingo/server/play/entryspot/BingoEntrySpot.java",
            "systems/zlink/samples/bingo/server/play/entryspot/handlers/MatchBingoActorHandler.java",
            "systems/zlink/samples/bingo/server/play/entryspot/handlers/BingoEntrySpotActorJoinedHandler.java",
            "systems/zlink/samples/bingo/server/play/entryspot/handlers/BingoEntrySpotActorLeftHandler.java",
            "systems/zlink/samples/bingo/server/play/handlers/AllocateBingoRoomHandler.java",
            "systems/zlink/samples/bingo/server/play/handlers/BingoRoomDirectory.java",
            "systems/zlink/samples/bingo/server/play/handlers/EnsurePlayerActorHandler.java",
            "systems/zlink/samples/bingo/server/registry/RegistryHostFactory.java",
            "systems/zlink/samples/bingo/server/session/SessionServerHostFactory.java",
            "systems/zlink/samples/bingo/server/session/sessions/BingoSession.java",
            "systems/zlink/samples/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.java",
            "systems/zlink/samples/bingo/shared/configuration/SampleNames.java",
            "systems/zlink/samples/bingo/shared/configuration/SampleTopology.java",
            "systems/zlink/samples/bingo/shared/contracts/Messages.java"));

        String mainSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/BingoSample.java");
        String clientSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/client/BingoPlayerClient.java");
        String roomSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoRoomSpot.java");
        String publisherSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoNotificationPublisher.java");

        assertTrue(mainSource.contains("new BingoClientOptions(4)"),
            "Bingo sample must create four connector clients");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "Bingo sample must use connector public factory");
        assertTrue(clientSource.contains("ZLinkStreamDispatchMode.MANUAL"),
            "Bingo sample must verify manual dispatch connector path");
        assertTrue(mainSource.contains("List.of(7, 11, 42, 42)"),
            "Bingo sample must use deterministic draw sequence");
        assertTrue(roomSource.contains("\"player-2\", \"player-3\""),
            "Bingo sample must verify same-sequence winners");
        assertTrue(publisherSource.contains("BingoWinner"),
            "Bingo sample must push bound client notification");
    }

    @Test
    void bingoKotlinSampleMirrorsJavaRoleLayout() {
        assertSampleFilesExist("kotlin", "Bingo", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/BingoKotlinSample.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoClientApp.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoClientOptions.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoNotificationInbox.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoPlayerClient.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/ApiServerHostFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/AuthenticatePlayerHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/MatchBingoHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/PlayServerHostFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/actors/PlayerActor.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/actors/PlayerActorFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoCard.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoNotificationPublisher.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoRoomModels.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoRoomSpot.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/handlers/BingoRoomJoinHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/handlers/BingoRoomActorJoinedHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/handlers/BingoRoomActorLeftHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/handlers/BingoRoomSpotCreatedHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/handlers/BingoRoomTimerHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/handlers/StartBingoGameHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/entryspot/BingoEntrySpot.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/entryspot/handlers/MatchBingoActorHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/entryspot/handlers/BingoEntrySpotActorJoinedHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/entryspot/handlers/BingoEntrySpotActorLeftHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/handlers/AllocateBingoRoomHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/handlers/BingoRoomDirectory.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/handlers/EnsurePlayerActorHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/registry/RegistryHostFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/SessionServerHostFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/sessions/BingoSession.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.kt",
            "systems/zlink/samples/kotlin/bingo/shared/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/bingo/shared/configuration/SampleTopology.kt",
            "systems/zlink/samples/kotlin/bingo/shared/contracts/Messages.kt"));
    }

    @Test
    void streamingClientMirrorsConnectorSmokeGate() throws IOException {
        String source = sampleJavaSource(
            "StreamingClient",
            "systems/zlink/samples/streamingclient/StreamingClientSample.java");

        assertTrue(source.contains("ZLinkStreamDispatchMode.MANUAL"),
            "StreamingClient sample must use manual dispatch");
        assertTrue(source.contains(".send("),
            "StreamingClient sample must call send");
        assertTrue(source.contains(".request("),
            "StreamingClient sample must call request");
        assertTrue(source.contains(".on("),
            "StreamingClient sample must register handler");
        assertTrue(source.contains("pendingDispatchCount() == 1"),
            "StreamingClient sample must assert manual queueing");
        assertTrue(source.contains("onConnectionStateChanged"),
            "StreamingClient sample must observe state changes");
        assertTrue(source.contains("onDisconnected"),
            "StreamingClient sample must observe disconnect");
        assertTrue(source.contains("disconnectAsync()"),
            "StreamingClient sample must simulate transport disconnect");
        assertTrue(source.contains("reconnectAsync()"),
            "StreamingClient sample must reconnect the same connector");
    }

    private static Path samplesRoot() {
        return frameworkJavaRoot().resolve("samples");
    }

    private static Path frameworkJavaRoot() {
        return Path.of(System.getProperty("user.dir")).getParent();
    }

    private static String sampleJavaSource(String sample, String relativePath) throws IOException {
        return Files.readString(samplesRoot()
            .resolve("java")
            .resolve(sample)
            .resolve("src/main/java")
            .resolve(relativePath));
    }

    private static void assertSampleFilesExist(
        String language,
        String sample,
        String sourceRoot,
        List<String> relativePaths) {
        Path sampleSourceRoot = samplesRoot()
            .resolve(language)
            .resolve(sample)
            .resolve(sourceRoot);
        for (String relativePath : relativePaths) {
            assertTrue(Files.isRegularFile(sampleSourceRoot.resolve(relativePath)),
                "missing " + language + "/" + sample + " sample source " + relativePath);
        }
    }

    private static boolean isSampleSource(Path path) {
        String pathText = path.toString();
        return pathText.endsWith(".java") || pathText.endsWith(".kt");
    }

    private static List<String> forbiddenLines(Path path) {
        try {
            String content = Files.readString(path);
            return FORBIDDEN_SAMPLE_PATTERNS.stream()
                .filter(content::contains)
                .toList();
        } catch (IOException ex) {
            throw new IllegalStateException("failed to read " + path, ex);
        }
    }
}
