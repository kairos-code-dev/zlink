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

    private static final List<String> DOTNET_DIRECT_SAMPLE_PROJECTS = List.of(
        "Client",
        "Server",
        "Shared");

    private static final List<String> DOTNET_GATEWAY_SAMPLE_PROJECTS = List.of(
        "Client",
        "Server/Api",
        "Server/Play",
        "Server/Registry",
        "Server/Session",
        "Shared");

    private static final List<String> FORBIDDEN_SAMPLE_PATTERNS = List.of(
        "import systems.zlink.runtime.",
        "import systems.zlink.internal.",
        "RouteStore",
        "MetadataStore",
        "RemoteAddressResolver",
        "BingoNotificationLoopbackServer",
        "BingoRoomState room",
        "new BingoRoomState",
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

            assertDotNetProjectLayout(languageRoot.resolve("TicTacToe"), DOTNET_DIRECT_SAMPLE_PROJECTS);
            assertDotNetProjectLayout(languageRoot.resolve("TicTacToe.SessionGateway"), DOTNET_GATEWAY_SAMPLE_PROJECTS);
            assertDotNetProjectLayout(languageRoot.resolve("Bingo"), DOTNET_GATEWAY_SAMPLE_PROJECTS);
        }
    }

    @Test
    void roleBasedSamplesDoNotCollapseIntoSingleGradleRun() throws IOException {
        for (String language : REQUIRED_LANGUAGES) {
            for (String sample : List.of("TicTacToe", "TicTacToe.SessionGateway", "Bingo")) {
                Path sampleRoot = samplesRoot().resolve(language).resolve(sample);
                String script = Files.readString(sampleRoot.resolve("run_sample.sh"));
                assertFalse(script.matches("(?s).*\\n\\s*gradle\\s+run\\s+--quiet\\s*\\n?.*"),
                    language + "/" + sample + " must start the same role entry points as the .NET sample");
                assertTrue(script.contains(":Client:run")
                        || script.contains("Client/build/install")
                        || script.contains("/Client/"),
                    language + "/" + sample + " runner must execute a distinct Client role");
                if (!sample.equals("TicTacToe")) {
                    assertTrue(script.contains(":Server:Registry:run")
                            || script.contains("Server/Registry/build/install")
                            || script.contains("/Server/Registry/"),
                        language + "/" + sample + " runner must execute a distinct Registry role");
                    assertTrue(script.contains(":Server:Api:run")
                            || script.contains("Server/Api/build/install")
                            || script.contains("/Server/Api/"),
                        language + "/" + sample + " runner must execute a distinct Api role");
                    assertTrue(script.contains(":Server:Play:run")
                            || script.contains("Server/Play/build/install")
                            || script.contains("/Server/Play/"),
                        language + "/" + sample + " runner must execute a distinct Play role");
                    assertTrue(script.contains(":Server:Session:run")
                            || script.contains("Server/Session/build/install")
                            || script.contains("/Server/Session/"),
                        language + "/" + sample + " runner must execute a distinct Session role");
                }
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
            "systems/zlink/samples/tictactoe/sessiongateway/TicTacToeSessionGatewaySample.java"));
        assertNoSampleSourcesUnder("java", "TicTacToe.SessionGateway", "src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/client",
            "systems/zlink/samples/tictactoe/sessiongateway/server",
            "systems/zlink/samples/tictactoe/sessiongateway/shared"));
        assertSampleFilesExist("java", "TicTacToe.SessionGateway", "Client/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/client/Program.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchClient.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchClientOptions.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorNotificationInbox.java"));
        assertSampleFilesExist("java", "TicTacToe.SessionGateway", "Server/Api/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/Program.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/ApiServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/handlers/AuthenticateActorHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/handlers/CreateMatchHandler.java"));
        assertSampleFilesExist("java", "TicTacToe.SessionGateway", "Server/Play/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/Program.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/PlayServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/handlers/CreateMatchRoomHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/handlers/EnsurePlayerActorHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameDirectory.java",
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
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorLeftHandler.java"));
        assertSampleFilesExist("java", "TicTacToe.SessionGateway", "Server/Registry/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/server/registry/Program.java"));
        assertSampleFilesExist("java", "TicTacToe.SessionGateway", "Server/Session/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/Program.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/SessionServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/SessionRelaySpot.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/PlayerSession.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/PlayerSessionDirectory.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/AuthenticateSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/CreateMatchSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.java"));
        assertSampleFilesExist("java", "TicTacToe.SessionGateway", "Shared/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/sessiongateway/shared/actors/PlayerActor.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/actors/PlayerActorFactory.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/configuration/SampleNames.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/configuration/SampleTopology.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/contracts/Messages.java",
            "systems/zlink/samples/tictactoe/sessiongateway/shared/contracts/PlayResponseEnvelope.java"));

        String sessionServerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/SessionServer.java");
        String playServerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/PlayServer.java");
        String apiServerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Api/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/ApiServer.java");
        String clientSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Client/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchClient.java");
        String playerClientSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Client/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.java");
        String playerSessionSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/PlayerSession.java");
        String joinSessionHandlerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.java");
        String placeSessionHandlerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.java");
        String relaySource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.java");
        String notificationPublisherSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/GameNotificationPublisher.java");
        String playDirectorySource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameDirectory.java");
        String playGameSpotSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameSpot.java");
        String mainSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/TicTacToeSessionGatewaySample.java");
        String apiProgramSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Api/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/Program.java");
        String playProgramSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/Program.java");
        String registryProgramSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Registry/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/registry/Program.java");
        String sessionProgramSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/Program.java");

        assertTrue(sessionServerSource.contains("attachActorGateway(SampleNames.SessionRelayNode)")
                || sessionServerSource.contains("attachActorGateway(\"session-relay\")"),
            "SessionGateway sample must attach stream node to local ActorGateway SpotNode");
        assertTrue(mainSource.contains("Program.start()")
                && !mainSource.contains("ZLinkFramework.start")
                && apiProgramSource.contains("@SpringBootApplication")
                && playProgramSource.contains("@SpringBootApplication")
                && registryProgramSource.contains("@SpringBootApplication")
                && sessionProgramSource.contains("@SpringBootApplication")
                && apiProgramSource.contains("ZLinkFrameworkOptionsCustomizer")
                && playProgramSource.contains("ZLinkFrameworkOptionsCustomizer")
                && registryProgramSource.contains("ZLinkEmbeddedRegistryOptions")
                && sessionProgramSource.contains("ZLinkFrameworkOptionsCustomizer"),
            "SessionGateway server roles must run through Spring Boot lifecycle beans");
        assertFalse(apiProgramSource.contains("CountDownLatch")
                || playProgramSource.contains("CountDownLatch")
                || registryProgramSource.contains("CountDownLatch")
                || sessionProgramSource.contains("CountDownLatch")
                || apiProgramSource.contains("ZLinkFramework.start")
                || playProgramSource.contains("ZLinkFramework.start")
                || registryProgramSource.contains("ZLinkRegistry.start")
                || sessionProgramSource.contains("ZLinkFramework.start"),
            "SessionGateway role entry points must not directly start ZLink or keep it alive with CountDownLatch");
        assertTrue(playServerSource.contains("useRegistrySpotRemoteAddresses"),
            "SessionGateway sample must use registry-backed Spot remote addresses");
        assertTrue(apiServerSource.contains("addHandlersFromPackageOf")
                && apiServerSource.contains("addHandlerGroup(\"api\")")
                && playServerSource.contains("addHandlersFromPackageOf")
                && playServerSource.contains("addHandlerGroup(\"play\")"),
            "SessionGateway Api/Play roles must use annotation-discovered handler groups");
        assertFalse(mainSource.contains("RecordingStreamNodeBuilder"),
            "SessionGateway sample must not replace stream node configuration with a recording builder");
        assertTrue(clientSource.contains("validateFinalState")
                && clientSource.contains("XXXOO....")
                && clientSource.contains("validateNotifications"),
            "SessionGateway Client role must validate gameplay, reconnect, and push notifications");
        assertFalse(clientSource.contains("ZLinkFramework"),
            "SessionGateway Client role must not start or depend on the in-process framework");
        assertFalse(clientSource.contains("systems.zlink.samples.tictactoe.sessiongateway.server."),
            "SessionGateway Client role must not import server implementation");
        assertFalse(clientSource.contains("TicTacToeSessionGatewaySample"),
            "SessionGateway Client role must not invoke the integrated sample");
        assertTrue(playerClientSource.contains("ZLinkStreamConnectorFactory.create")
                && playerClientSource.contains("submitAsync()"),
            "SessionGateway player client must use the public stream connector request API");
        assertTrue(playerClientSource.contains("AuthenticateReq")
                && playerClientSource.contains("CreateMatchReq")
                && playerClientSource.contains("JoinMatchReq")
                && playerClientSource.contains("PlaceMarkReq"),
            "SessionGateway player client must exercise the full session request flow");
        assertTrue(playerClientSource.contains("join(")
                && playerClientSource.contains("placeMark("),
            "SessionGateway player client must expose join and place-mark operations");
        assertTrue(playerSessionSource.contains("ZLinkSessionContext")
                && playerSessionSource.contains("context.actors()"),
            "SessionGateway sample session must use framework-owned ZLinkSessionContext");
        assertTrue(joinSessionHandlerSource.contains("JoinMatchReq")
                && joinSessionHandlerSource.contains("SampleNames.PlayChannel")
                && joinSessionHandlerSource.contains("PlayNotificationRelay"),
            "SessionGateway join packet handler must relay gameplay through the Play role notification envelope");
        assertTrue(placeSessionHandlerSource.contains("PlaceMarkReq")
                && placeSessionHandlerSource.contains("SampleNames.PlayChannel")
                && placeSessionHandlerSource.contains("PlayNotificationRelay"),
            "SessionGateway place-mark packet handler must relay gameplay through the Play role notification envelope");
        assertFalse(joinSessionHandlerSource.contains("TurnChangedPacket")
                || placeSessionHandlerSource.contains("GameEndedPacket"),
            "Session handlers must not own gameplay notification packet construction");
        assertTrue(notificationPublisherSource.contains("TurnChangedPacket")
                && notificationPublisherSource.contains("GameEndedPacket")
                && relaySource.contains("PlayerSessionDirectory"),
            "Play role must create gameplay notifications and Session role must relay them to bound sessions");
        assertFalse(playerSessionSource.contains("TicTacToeGameDirectory")
                || playerSessionSource.contains("ConcurrentHashMap")
                || playerSessionSource.contains("new TicTacToeGameSpot"),
            "SessionGateway PlayerSession must not own game storage; Play role owns match state");
        assertTrue(playDirectorySource.contains("ConcurrentHashMap")
                && playDirectorySource.contains("TicTacToeGameSpot"),
            "SessionGateway Play role must own match directory state");
        assertTrue(playGameSpotSource.contains("placeMark")
                && playGameSpotSource.contains("winner"),
            "SessionGateway Play role must own gameplay state transitions");
        assertFalse(clientSource.contains("systems.zlink.contracts.service.spot.ActorRef"),
            "SessionGateway sample must not import binding ActorRef");
        assertFalse(clientSource.contains("new ActorRef("),
            "SessionGateway sample must not construct binding ActorRef");
        assertFalse(clientSource.contains("ZLinkSpotRemoteAddressResolver"),
            "session handler must not call actor remote address resolver");
    }

    @Test
    void ticTacToeSessionGatewayKotlinSampleMirrorsJavaRoleLayout() throws IOException {
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/TicTacToeSessionGatewayKotlinSample.kt"));
        assertNoSampleSourcesUnder("kotlin", "TicTacToe.SessionGateway", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared"));
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "Client/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/Program.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchClient.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchClientOptions.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorNotificationInbox.kt"));
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "Server/Api/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/Program.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/ApiServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/handlers/AuthenticateActorHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/handlers/CreateMatchHandler.kt"));
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "Server/Play/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/Program.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/PlayServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/handlers/CreateMatchRoomHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/handlers/EnsurePlayerActorHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameDirectory.kt",
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
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorLeftHandler.kt"));
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "Server/Registry/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/registry/Program.kt"));
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "Server/Session/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/Program.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/SessionServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/SessionRelaySpot.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/PlayerSession.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/PlayerSessionDirectory.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/AuthenticateSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/CreateMatchSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.kt"));
        assertSampleFilesExist("kotlin", "TicTacToe.SessionGateway", "Shared/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/actors/PlayerActor.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/actors/PlayerActorFactory.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/configuration/SampleTopology.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/contracts/Messages.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/contracts/PlayResponseEnvelope.kt"));

        String sessionServerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/SessionServer.kt");
        String mainSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/TicTacToeSessionGatewayKotlinSample.kt");
        String playServerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/PlayServer.kt");
        String apiServerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Api/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/ApiServer.kt");
        String clientSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchClient.kt");
        String playerClientSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.kt");
        String playerSessionSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/PlayerSession.kt");
        String joinSessionHandlerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.kt");
        String placeSessionHandlerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.kt");
        String relaySource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.kt");
        String notificationPublisherSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/GameNotificationPublisher.kt");
        String playDirectorySource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameDirectory.kt");
        String playGameSpotSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameSpot.kt");
        String apiProgramSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Api/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/Program.kt");
        String playProgramSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/Program.kt");
        String registryProgramSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Registry/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/registry/Program.kt");
        String sessionProgramSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/Program.kt");

        assertTrue(sessionServerSource.contains("attachActorGateway(SampleNames.SessionRelayNode)")
                || sessionServerSource.contains("attachActorGateway(\"session-relay\")"),
            "Kotlin SessionGateway sample must attach stream node to local ActorGateway SpotNode");
        assertTrue(mainSource.contains("Application.start()")
                && !mainSource.contains("ZLinkFramework.start")
                && apiProgramSource.contains("@SpringBootApplication")
                && playProgramSource.contains("@SpringBootApplication")
                && registryProgramSource.contains("@SpringBootApplication")
                && sessionProgramSource.contains("@SpringBootApplication")
                && apiProgramSource.contains("ZLinkFrameworkOptionsCustomizer")
                && playProgramSource.contains("ZLinkFrameworkOptionsCustomizer")
                && registryProgramSource.contains("ZLinkEmbeddedRegistryOptions")
                && sessionProgramSource.contains("ZLinkFrameworkOptionsCustomizer"),
            "Kotlin SessionGateway server roles must run through Spring Boot lifecycle beans");
        assertFalse(apiProgramSource.contains("CountDownLatch")
                || playProgramSource.contains("CountDownLatch")
                || registryProgramSource.contains("CountDownLatch")
                || sessionProgramSource.contains("CountDownLatch")
                || apiProgramSource.contains("ZLinkFramework.start")
                || playProgramSource.contains("ZLinkFramework.start")
                || registryProgramSource.contains("ZLinkRegistry.start")
                || sessionProgramSource.contains("ZLinkFramework.start"),
            "Kotlin SessionGateway role entry points must not directly start ZLink or keep it alive with CountDownLatch");
        assertTrue(playServerSource.contains("useRegistrySpotRemoteAddresses"),
            "Kotlin SessionGateway sample must use registry-backed Spot remote addresses");
        assertTrue(apiServerSource.contains("addHandlersFromPackageOf")
                && apiServerSource.contains("addHandlerGroup(\"api\")")
                && playServerSource.contains("addHandlersFromPackageOf")
                && playServerSource.contains("addHandlerGroup(\"play\")"),
            "Kotlin SessionGateway Api/Play roles must use annotation-discovered handler groups");
        assertTrue(clientSource.contains("validateFinalState")
                && clientSource.contains("XXXOO....")
                && clientSource.contains("validateNotifications"),
            "Kotlin SessionGateway Client role must validate gameplay, reconnect, and push notifications");
        assertFalse(clientSource.contains("ZLinkFramework"),
            "Kotlin SessionGateway Client role must not start or depend on the in-process framework");
        assertFalse(clientSource.contains("systems.zlink.samples.kotlin.tictactoe.sessiongateway.server."),
            "Kotlin SessionGateway Client role must not import server implementation");
        assertFalse(clientSource.contains("sessiongateway.main()"),
            "Kotlin SessionGateway Client role must not invoke the integrated sample");
        assertTrue(playerClientSource.contains("ZLinkStreamConnectorFactory.create")
                && playerClientSource.contains("submitAsync()"),
            "Kotlin SessionGateway player client must use the public stream connector request API");
        assertTrue(playerClientSource.contains("AuthenticateReq")
                && playerClientSource.contains("CreateMatchReq")
                && playerClientSource.contains("JoinMatchReq")
                && playerClientSource.contains("PlaceMarkReq"),
            "Kotlin SessionGateway player client must exercise the full session request flow");
        assertTrue(playerClientSource.contains("join(")
                && playerClientSource.contains("placeMark("),
            "Kotlin SessionGateway player client must expose join and place-mark operations");
        assertTrue(playerSessionSource.contains("ZLinkSessionContext")
                && playerSessionSource.contains("context.actors()"),
            "Kotlin SessionGateway sample session must use framework-owned ZLinkSessionContext");
        assertTrue(joinSessionHandlerSource.contains("JoinMatchReq")
                && joinSessionHandlerSource.contains("SampleNames.PlayChannel")
                && joinSessionHandlerSource.contains("PlayNotificationRelay"),
            "Kotlin SessionGateway join packet handler must relay gameplay through the Play role notification envelope");
        assertTrue(placeSessionHandlerSource.contains("PlaceMarkReq")
                && placeSessionHandlerSource.contains("SampleNames.PlayChannel")
                && placeSessionHandlerSource.contains("PlayNotificationRelay"),
            "Kotlin SessionGateway place-mark packet handler must relay gameplay through the Play role notification envelope");
        assertFalse(joinSessionHandlerSource.contains("TurnChangedPacket")
                || placeSessionHandlerSource.contains("GameEndedPacket"),
            "Kotlin Session handlers must not own gameplay notification packet construction");
        assertTrue(notificationPublisherSource.contains("TurnChangedPacket")
                && notificationPublisherSource.contains("GameEndedPacket")
                && relaySource.contains("PlayerSessionDirectory"),
            "Kotlin Play role must create gameplay notifications and Session role must relay them to bound sessions");
        assertFalse(playerSessionSource.contains("TicTacToeGameDirectory")
                || playerSessionSource.contains("ConcurrentHashMap")
                || playerSessionSource.contains("TicTacToeGameSpot("),
            "Kotlin SessionGateway PlayerSession must not own game storage; Play role owns match state");
        assertTrue(playDirectorySource.contains("ConcurrentHashMap")
                && playDirectorySource.contains("TicTacToeGameSpot"),
            "Kotlin SessionGateway Play role must own match directory state");
        assertTrue(playGameSpotSource.contains("placeMark")
                && playGameSpotSource.contains("winner"),
            "Kotlin SessionGateway Play role must own gameplay state transitions");
        assertFalse(clientSource.contains("systems.zlink.contracts.service.spot.ActorRef"),
            "Kotlin SessionGateway sample must not import binding ActorRef");
        assertFalse(clientSource.contains("ZLinkSpotRemoteAddressResolver"),
            "Kotlin session handler must not call actor remote address resolver");
    }

    @Test
    void ticTacToeDirectSampleUsesFrameworkRuntimePublicFacade() throws IOException {
        assertSampleFilesExist("java", "TicTacToe", "src/main/java", List.of(
            "systems/zlink/samples/tictactoe/TicTacToeSample.java"));
        assertNoSampleSourcesUnder("java", "TicTacToe", "src/main/java",
            List.of(
                "systems/zlink/samples/tictactoe/client",
                "systems/zlink/samples/tictactoe/server",
                "systems/zlink/samples/tictactoe/shared"));
        assertSampleFilesExist("java", "TicTacToe", "Client/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/client/TicTacToeClientArguments.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClient.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClientOptions.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClientResult.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeSampleDefaults.java"));
        assertTrue(sampleFileContains("java", "TicTacToe", "Client",
                "README.md", "Tic Tac Toe Client"),
            "Java TicTacToe Client role must include a standalone README");
        assertSampleFilesExist("java", "TicTacToe", "Server/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/server/api/ApiServer.java",
            "systems/zlink/samples/tictactoe/server/api/handlers/AuthenticatePlayerHandler.java",
            "systems/zlink/samples/tictactoe/server/api/handlers/CreateGameHttpHandler.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleLogging.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleNames.java",
            "systems/zlink/samples/tictactoe/server/configuration/SamplePorts.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleSettings.java",
            "systems/zlink/samples/tictactoe/server/play/PlayServer.java",
            "systems/zlink/samples/tictactoe/server/play/actors/PlayActor.java",
            "systems/zlink/samples/tictactoe/server/play/actors/PlayActorFactory.java",
            "systems/zlink/samples/tictactoe/server/play/entryspot/PlayEntrySpot.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/TicTacToeGame.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/PlayActorPlaceMarkHandler.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameJoinHandler.java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameTimerHandler.java",
            "systems/zlink/samples/tictactoe/server/play/handlers/CreateGameHandler.java",
            "systems/zlink/samples/tictactoe/server/play/sessions/PlaySession.java"));
        assertSampleFilesExist("java", "TicTacToe", "Shared/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/shared/contracts/GameState.java",
            "systems/zlink/samples/tictactoe/shared/contracts/GameStateNotify.java",
            "systems/zlink/samples/tictactoe/shared/contracts/AuthenticatePlayerReq.java",
            "systems/zlink/samples/tictactoe/shared/contracts/AuthenticatePlayerRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/AuthenticateReq.java",
            "systems/zlink/samples/tictactoe/shared/contracts/AuthenticateRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/CreateGameHttpReq.java",
            "systems/zlink/samples/tictactoe/shared/contracts/CreateGameHttpRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/CreateGameReq.java",
            "systems/zlink/samples/tictactoe/shared/contracts/CreateGameRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/JoinGameReq.java",
            "systems/zlink/samples/tictactoe/shared/contracts/JoinGameRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/PlaceMarkReq.java",
            "systems/zlink/samples/tictactoe/shared/contracts/PlaceMarkRes.java",
            "systems/zlink/samples/tictactoe/shared/contracts/PlayerJoinedNotify.java",
            "systems/zlink/samples/tictactoe/shared/contracts/TicTacToeGameJoinReq.java",
            "systems/zlink/samples/tictactoe/shared/contracts/TicTacToeGameJoinRes.java"));
        assertTrue(sampleFileContains("java", "TicTacToe", "Client/src/main/java",
                "systems/zlink/samples/tictactoe/client/Program.java", "TicTacToeClientArguments.parse"),
            "Java TicTacToe Client role Program must live in the Client project folder");
        assertTrue(sampleFileContains("java", "TicTacToe", "Server/src/main/java",
                "systems/zlink/samples/tictactoe/server/Program.java", "PlayServer.start(settings)"),
            "Java TicTacToe Server role Program must live in the Server project folder");

        String mainSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/TicTacToeSample.java");
        String serverProgramSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/Program.java");
        String serverBuildSource = sampleFile(
            "java",
            "TicTacToe",
            "Server",
            "build.gradle.kts");
        String clientSource = sampleJavaSource(
            "TicTacToe",
            "Client/src/main/java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClient.java");
        String clientResultSource = sampleJavaSource(
            "TicTacToe",
            "Client/src/main/java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClientResult.java");
        String apiSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/api/ApiServer.java");
        String authHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/api/handlers/AuthenticatePlayerHandler.java");
        String createGameHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/api/handlers/CreateGameHttpHandler.java");
        String settingsSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleSettings.java");
        String playSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/PlayServer.java");
        String playActorSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/actors/PlayActor.java");
        String entrySpotSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/entryspot/PlayEntrySpot.java");
        String gameSpotSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/TicTacToeGame.java");
        String playSessionSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/sessions/PlaySession.java");

        assertTrue(mainSource.contains("PlayServer.start(settings)")
                && mainSource.contains("ApiServer.start(settings)")
                && mainSource.contains("ConfigurableApplicationContext"),
            "TicTacToe direct Java sample must start roles through the Spring Boot public facade");
        assertTrue(serverProgramSource.contains("case \"client\"")
                && serverProgramSource.contains("new TicTacToeClient().run")
                && serverProgramSource.contains("result.writeTo(System.out)")
                && serverBuildSource.contains("implementation(project(\":Client\"))"),
            "TicTacToe Java Server role must expose the .NET-style client run mode through the aggregate server entry point");
        assertFalse(serverProgramSource.contains("CountDownLatch"),
            "TicTacToe Java Server role must rely on Spring lifecycle keep-alive instead of manual latches");
        assertTrue(mainSource.contains("SampleSettings.fromArgs(args).withEphemeralDefaults()")
                && apiSource.contains("ZLinkFrameworkOptionsCustomizer")
                && apiSource.contains("@SpringBootApplication")
                && apiSource.contains("SpringApplicationBuilder")
                && apiSource.contains(".web(WebApplicationType.SERVLET)")
                && apiSource.contains("setKeepAlive(true)")
                && apiSource.contains("options.codecs().addJson()")
                && playSource.contains("ZLinkFrameworkOptionsCustomizer")
                && playSource.contains("@SpringBootApplication")
                && playSource.contains("SpringApplicationBuilder")
                && playSource.contains(".web(WebApplicationType.NONE)")
                && playSource.contains("setKeepAlive(true)")
                && playSource.contains("options.codecs().addJson()"),
            "TicTacToe direct Api and Play framework hosts must enable JSON codecs and expose HTTP create-game with shared settings");
        assertTrue(settingsSource.contains("withEphemeralDefaults()")
                && settingsSource.contains("SamplePorts.reserve()")
                && settingsSource.contains("--api-bind")
                && settingsSource.contains("--api-url")
                && settingsSource.contains("--api-channel-endpoint")
                && settingsSource.contains("--play-channel-endpoint")
                && settingsSource.contains("--play-router-endpoint")
                && settingsSource.contains("--play-endpoint"),
            "TicTacToe direct sample must expose .NET-style sample settings instead of fixed topology constants");
        assertTrue(clientSource.contains("HttpClient")
                && clientSource.contains("CreateGameHttpReq")
                && clientSource.contains("CreateGameHttpRes")
                && clientSource.contains(".resolve(\"/games\")")
                && !clientSource.contains(".requestToChannel("),
            "TicTacToe direct client must create games through the HTTP API path");
        assertTrue(playSessionSource.contains("new AuthenticatePlayerReq(request.accessToken())")
                && playSessionSource.contains(".submitAsync(AuthenticatePlayerRes.class)")
                && authHandlerSource.contains("CompletionStage<AuthenticatePlayerRes>")
                && authHandlerSource.contains("AuthenticatePlayerReq request"),
            "TicTacToe direct Play session AuthenticatePlayer path must use typed request and response contracts");
        assertTrue(createGameHandlerSource.contains("@RestController")
                && createGameHandlerSource.contains("@PostMapping(\"/games\")")
                && createGameHandlerSource.contains("CompletionStage<CreateGameHttpRes>")
                && createGameHandlerSource.contains("CreateGameHttpReq")
                && createGameHandlerSource.contains("CreateGameHttpRes")
                && createGameHandlerSource.contains("new CreateGameReq")
                && createGameHandlerSource.contains(".requestToChannel(")
                && createGameHandlerSource.contains(".submitAsync(CreateGameRes.class)")
                && !createGameHandlerSource.contains("HttpExchange")
                && !createGameHandlerSource.contains("HttpServer"),
            "TicTacToe HTTP create-game endpoint must run as a Spring controller and translate to the typed Play channel request");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "TicTacToe Client role must use the public stream connector for play requests");
        assertTrue(clientSource.contains("ZLinkStreamJson.request")
                && clientSource.contains("new AuthenticateReq(options.xActorId())")
                && clientSource.contains("new JoinGameReq(game.gameId())")
                && clientSource.contains("new PlaceMarkReq(2)")
                && !clientSource.contains("game.gameId() + \"|\""),
            "TicTacToe stream client path must use typed JSON stream request contracts");
        assertTrue(clientSource.contains("ZLinkStreamJson.on(host, GameStateNotify.class")
                && clientSource.contains("ZLinkStreamJson.on(guest, GameStateNotify.class")
                && clientSource.contains("ZLinkStreamJson.on(host, PlayerJoinedNotify.class")
                && clientSource.contains("ZLinkStreamJson.on(guest, PlayerJoinedNotify.class")
                && clientSource.contains("ZLinkStreamDispatchMode.AUTO")
                && clientSource.contains("CompletableFuture.delayedExecutor(250, TimeUnit.MILLISECONDS)")
                && clientResultSource.contains("List<GameStateNotify>")
                && clientResultSource.contains("List<PlayerJoinedNotify>")
                && clientResultSource.contains("finalState()")
                && clientResultSource.contains("notifications:")
                && !clientResultSource.contains("pushes")
                && !clientResultSource.contains("GameWon"),
            "TicTacToe direct client must subscribe to typed stream notifications and report actual received packets");
        assertFalse(clientSource.contains("systems.zlink.samples.tictactoe.server."),
            "TicTacToe Client role must not import server implementation");
        assertFalse(clientSource.contains("TicTacToeGameDirectory"),
            "TicTacToe Client role must not access server game storage directly");
        assertTrue(apiSource.contains(".addClientServerChannel(")
                && apiSource.contains("settings.apiChannelEndpoint()")
                && apiSource.contains("settings.playChannelEndpoint()")
                && apiSource.contains("addHandlersFromPackageOf")
                && apiSource.contains("addHandlerGroup(\"api\")")
                && !apiSource.contains("addRequestHandler"),
            "TicTacToe direct sample must expose the Api server role through annotation-discovered handlers");
        assertTrue(authHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && authHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayer\")")
                && createGameHandlerSource.contains("@RequestBody")
                && createGameHandlerSource.contains("CreateGameHttpReq")
                && createGameHandlerSource.contains("CreateGameHttpRes"),
            "TicTacToe direct Api handlers must use annotation-based auth and HTTP create-game mapping");
        assertTrue(playSource.contains(".addSpotMesh("),
            "TicTacToe direct sample must expose the Play Spot role");
        assertTrue(playSource.contains("addHandlersFromPackageOf")
                && playSource.contains("settings.apiChannelEndpoint()")
                && playSource.contains("settings.playChannelEndpoint()")
                && playSource.contains("settings.playRouterEndpoint()")
                && playSource.contains("settings.playEndpoint()")
                && playSource.contains("addHandlerGroup(SampleNames.PlayChannel)"),
            "TicTacToe Play role must expose annotation-discovered play channel handlers");
        String playCreateGameHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/handlers/CreateGameHandler.java");
        assertTrue(playCreateGameHandlerSource.contains("CompletionStage<CreateGameRes>")
                && playCreateGameHandlerSource.contains("ZLinkSpotManager")
                && playCreateGameHandlerSource.contains("@ZLinkHandlerGroup(SampleNames.PlayChannel)")
                && playCreateGameHandlerSource.contains("CreateGameReq request")
                && playCreateGameHandlerSource.contains("spots.createAsync(TicTacToeGame.class)")
                && playCreateGameHandlerSource.contains("new CreateGameRes("),
            "TicTacToe Play CreateGame handler must create a Spot and reply with typed contracts");
        String gameJoinReqSource = sampleJavaSource(
            "TicTacToe",
            "Shared/src/main/java",
            "systems/zlink/samples/tictactoe/shared/contracts/TicTacToeGameJoinReq.java");
        String gameJoinResSource = sampleJavaSource(
            "TicTacToe",
            "Shared/src/main/java",
            "systems/zlink/samples/tictactoe/shared/contracts/TicTacToeGameJoinRes.java");
        String playJoinHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameJoinHandler.java");
        String playActorJoinHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/entryspot/handlers/PlayActorJoinGameHandler.java");
        String playPlaceMarkHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/PlayActorPlaceMarkHandler.java");
        String gameCreatedHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameCreatedHandler.java");
        String gameActorJoinedHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameActorJoinedHandler.java");
        String gameActorLeftHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameActorLeftHandler.java");
        String gameTimerHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/gamespots/handlers/TicTacToeGameTimerHandler.java");
        String entryActorJoinedHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/entryspot/handlers/PlayEntrySpotActorJoinedHandler.java");
        String entryActorLeftHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/entryspot/handlers/PlayEntrySpotActorLeftHandler.java");
        assertTrue(playSessionSource.contains("ZLinkStreamJson.decode")
                && playSessionSource.contains("new AuthenticatePlayerReq(request.accessToken())")
                && playSessionSource.contains("actorId = authenticated.actorId()")
                && playSessionSource.contains("context.actors()::bindAsync")
                && playSessionSource.contains("requireActor().relayAsync(header, payload)")
                && !playSessionSource.contains("joinSpot(RoutingId.fromHex")
                && !playSessionSource.contains("split(\"\\\\|\")"),
            "TicTacToe Play stream session must authenticate through the Api role and relay actor packets");
        assertTrue(gameJoinReqSource.contains("String gameId")
                && gameJoinReqSource.contains("String actorId")
                && gameJoinResSource.contains("GameState state"),
            "TicTacToe direct sample must split client JoinGame contracts from Spot join contracts");
        assertTrue(playJoinHandlerSource.contains("CompletionStage<TicTacToeGameJoinRes>")
                && playJoinHandlerSource.contains("TicTacToeGameJoinReq request")
                && playJoinHandlerSource.contains("TicTacToeGameJoinRes")
                && playActorJoinHandlerSource.contains("new TicTacToeGameJoinReq")
                && playActorJoinHandlerSource.contains("new JoinGameRes")
                && playPlaceMarkHandlerSource.contains("PlaceMarkReq request"),
            "TicTacToe Play actor handlers must receive typed stream request contracts");
        assertTrue(gameSpotSource.contains("actor.joinGame")
                && gameSpotSource.contains("boundSession()")
                && gameSpotSource.contains(".send(new GameStateNotify")
                && gameSpotSource.contains(".send(message)"),
            "TicTacToe game Spot must own joined-game state transitions and typed bound-session notifications");
        assertTrue(gameSpotSource.contains("onInitializeAsync()")
                && gameSpotSource.contains("context.addTimer(")
                && gameSpotSource.contains("TicTacToeGameTimerHandler.class")
                && gameSpotSource.contains("onClosingAsync()")
                && gameSpotSource.contains("gameTick.cancelAsync()")
                && gameSpotSource.contains("TurnTimedOut")
                && gameSpotSource.contains("resetTurnDeadline()")
                && gameSpotSource.contains("tickAsync()")
                && gameSpotSource.contains("markCreated(List<Message> createParts)")
                && gameSpotSource.contains("ensureCreated()")
                && gameCreatedHandlerSource.contains("handleAsync(")
                && gameCreatedHandlerSource.contains("List<Message>")
                && gameCreatedHandlerSource.contains("game.markCreated(createParts)")
                && gameTimerHandlerSource.contains("implements ZLinkSpotTimerHandler<TicTacToeGame>")
                && gameTimerHandlerSource.contains("spot.tickAsync()"),
            "TicTacToe game Spot must mirror the .NET lifecycle, timer, and turn-timeout API usage");
        assertTrue(gameActorJoinedHandlerSource.contains("@ZLinkSpotPostActorJoined")
                && gameActorJoinedHandlerSource.contains("ZLinkSpotActorChangeResult")
                && gameActorLeftHandlerSource.contains("@ZLinkSpotActorLeft")
                && gameActorLeftHandlerSource.contains("ZLinkSpotActorChangeResult")
                && entryActorJoinedHandlerSource.contains("@ZLinkSpotPostActorJoined")
                && entryActorLeftHandlerSource.contains("@ZLinkSpotActorLeft"),
            "TicTacToe EntrySpot and GameSpot lifecycle handler files must use framework lifecycle annotations, not placeholder methods");
        assertTrue(playActorJoinHandlerSource.contains("request.gameId()"),
            "TicTacToe join handler must store the requested game id");
        assertTrue(playPlaceMarkHandlerSource.contains("actor.requireJoinedGame()"),
            "TicTacToe place handler must require actor join state");
        assertTrue(playActorSource.contains("joinedGameId"),
            "TicTacToe PlayActor must own joined game state");
        assertTrue(playActorSource.contains("joinGame"),
            "TicTacToe PlayActor must expose joinGame state transition");
        assertTrue(playActorSource.contains("requireJoinedGame"),
            "TicTacToe PlayActor must validate joined game state");
        assertTrue(entrySpotSource.contains("PlayEntrySpot(ZLinkEntrySpotContext context)")
                && !entrySpotSource.contains("public PlayEntrySpot()")
                && gameSpotSource.contains("TicTacToeGame(ZLinkSpotContext context)")
                && !gameSpotSource.contains("public TicTacToeGame()")
                && !gameSpotSource.contains("SampleSpotContext")
                && !gameSpotSource.contains("CompletedSpotOutbound")
                && !gameSpotSource.contains("TicTacToeGameDirectory"),
            "TicTacToe Spot instances must be created by the framework runtime, not sample-owned fallback contexts");
        assertTrue(playSource.contains(".addStreamNode("),
            "TicTacToe direct sample must register the STREAM entry point");
        assertFalse(mainSource.contains("CreateGameHandler"),
            "TicTacToe main must not collapse Play handler wiring into the entry point");
    }

    @Test
    void ticTacToeKotlinSampleMirrorsJavaRoleLayout() throws IOException {
        assertSampleFilesExist("kotlin", "TicTacToe", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/TicTacToeKotlinSample.kt"));
        assertNoSampleSourcesUnder("kotlin", "TicTacToe", "src/main/kotlin",
            List.of(
                "systems/zlink/samples/kotlin/tictactoe/client",
                "systems/zlink/samples/kotlin/tictactoe/server",
                "systems/zlink/samples/kotlin/tictactoe/shared"));
        assertSampleFilesExist("kotlin", "TicTacToe", "Client/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientArguments.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClient.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientOptions.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientResult.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeSampleDefaults.kt"));
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Client",
                "README.md", "Tic Tac Toe Client"),
            "Kotlin TicTacToe Client role must include a standalone README");
        assertSampleFilesExist("kotlin", "TicTacToe", "Server/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/AuthenticatePlayerHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/CreateGameHttpHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleLogging.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SamplePorts.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleSettings.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/PlayServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/actors/PlayActor.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/actors/PlayActorFactory.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/entryspot/PlayEntrySpot.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/TicTacToeGame.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/PlayActorPlaceMarkHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameJoinHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameTimerHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/handlers/CreateGameHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/sessions/PlaySession.kt"));
        assertSampleFilesExist("kotlin", "TicTacToe", "Shared/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/shared/contracts/Contracts.kt"));
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Client/src/main/kotlin",
                "systems/zlink/samples/kotlin/tictactoe/client/Program.kt", "TicTacToeClientArguments.parse"),
            "Kotlin TicTacToe Client role Program must live in the Client project folder");
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Server/src/main/kotlin",
                "systems/zlink/samples/kotlin/tictactoe/server/Program.kt", "PlayServer.start(settings)"),
            "Kotlin TicTacToe Server role Program must live in the Server project folder");

        String mainSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/TicTacToeKotlinSample.kt");
        String serverProgramSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/Program.kt");
        String serverBuildSource = sampleFile(
            "kotlin",
            "TicTacToe",
            "Server",
            "build.gradle.kts");
        String clientSource = sampleKotlinSource(
            "TicTacToe",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClient.kt");
        String clientResultSource = sampleKotlinSource(
            "TicTacToe",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientResult.kt");
        String apiSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServer.kt");
        String authHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/AuthenticatePlayerHandler.kt");
        String createGameHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/CreateGameHttpHandler.kt");
        String settingsSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleSettings.kt");
        String playSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/PlayServer.kt");
        String playActorSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/actors/PlayActor.kt");
        String entrySpotSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/entryspot/PlayEntrySpot.kt");
        String gameSpotSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/TicTacToeGame.kt");
        String playSessionSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/sessions/PlaySession.kt");

        assertTrue(mainSource.contains("PlayServer.start(settings)")
                && mainSource.contains("ApiServer.start(settings)"),
            "Kotlin TicTacToe direct sample must start roles through the Spring Boot public facade");
        assertTrue(serverProgramSource.contains("\"client\" -> runClient(settings)")
                && serverProgramSource.contains("TicTacToeClient().run")
                && serverProgramSource.contains("result.writeTo(System.out)")
                && serverBuildSource.contains("implementation(project(\":Client\"))"),
            "Kotlin TicTacToe Server role must expose the .NET-style client run mode through the aggregate server entry point");
        assertFalse(serverProgramSource.contains("CountDownLatch"),
            "Kotlin TicTacToe Server role must rely on Spring lifecycle keep-alive instead of manual latches");
        assertTrue(mainSource.contains("SampleSettings.fromArgs(args).withEphemeralDefaults()")
                && apiSource.contains("ZLinkFrameworkOptionsCustomizer")
                && apiSource.contains("@SpringBootApplication")
                && apiSource.contains("SpringApplicationBuilder")
                && apiSource.contains(".web(WebApplicationType.SERVLET)")
                && apiSource.contains("setKeepAlive(true)")
                && apiSource.contains("options.codecs().addJson()")
                && playSource.contains("ZLinkFrameworkOptionsCustomizer")
                && playSource.contains("@SpringBootApplication")
                && playSource.contains("SpringApplicationBuilder")
                && playSource.contains(".web(WebApplicationType.NONE)")
                && playSource.contains("setKeepAlive(true)")
                && playSource.contains("options.codecs().addJson()"),
            "Kotlin TicTacToe direct Api and Play framework hosts must enable JSON codecs and expose HTTP create-game with shared settings");
        assertTrue(settingsSource.contains("withEphemeralDefaults()")
                && settingsSource.contains("SamplePorts.reserve()")
                && settingsSource.contains("--api-bind")
                && settingsSource.contains("--api-url")
                && settingsSource.contains("--api-channel-endpoint")
                && settingsSource.contains("--play-channel-endpoint")
                && settingsSource.contains("--play-router-endpoint")
                && settingsSource.contains("--play-endpoint"),
            "Kotlin TicTacToe direct sample must expose .NET-style sample settings instead of fixed topology constants");
        assertTrue(clientSource.contains("HttpClient")
                && clientSource.contains("CreateGameHttpReq")
                && clientSource.contains("CreateGameHttpRes")
                && clientSource.contains(".resolve(\"/games\")")
                && !clientSource.contains(".requestToChannel("),
            "Kotlin TicTacToe direct client must create games through the HTTP API path");
        assertTrue(playSessionSource.contains("AuthenticatePlayerReq(request.accessToken)")
                && playSessionSource.contains(".submitAsync(AuthenticatePlayerRes::class.java)")
                && authHandlerSource.contains("CompletionStage<AuthenticatePlayerRes>")
                && authHandlerSource.contains("request: AuthenticatePlayerReq"),
            "Kotlin TicTacToe direct Play session AuthenticatePlayer path must use typed request and response contracts");
        assertTrue(createGameHandlerSource.contains("@RestController")
                && createGameHandlerSource.contains("@PostMapping(\"/games\")")
                && createGameHandlerSource.contains("CompletionStage<CreateGameHttpRes>")
                && createGameHandlerSource.contains("CreateGameHttpReq")
                && createGameHandlerSource.contains("CreateGameHttpRes")
                && createGameHandlerSource.contains("CreateGameReq")
                && createGameHandlerSource.contains(".requestToChannel(")
                && createGameHandlerSource.contains(".submitAsync(CreateGameRes::class.java)")
                && !createGameHandlerSource.contains("HttpExchange")
                && !createGameHandlerSource.contains("HttpServer"),
            "Kotlin TicTacToe HTTP create-game endpoint must run as a Spring controller and translate to the typed Play channel request");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "Kotlin TicTacToe Client role must use the public stream connector for play requests");
        assertTrue(clientSource.contains("ZLinkStreamJson.request")
                && clientSource.contains("AuthenticateReq(options.xActorId)")
                && clientSource.contains("JoinGameReq(game.gameId)")
                && clientSource.contains("PlaceMarkReq(2)")
                && !clientSource.contains("game.gameId}|"),
            "Kotlin TicTacToe stream client path must use typed JSON stream request contracts");
        assertTrue(clientSource.contains("ZLinkStreamJson.on(hostStream, GameStateNotify::class.java")
                && clientSource.contains("ZLinkStreamJson.on(guestStream, GameStateNotify::class.java")
                && clientSource.contains("ZLinkStreamJson.on(hostStream, PlayerJoinedNotify::class.java")
                && clientSource.contains("ZLinkStreamJson.on(guestStream, PlayerJoinedNotify::class.java")
                && clientSource.contains("ZLinkStreamDispatchMode.AUTO")
                && clientSource.contains("CompletableFuture.delayedExecutor(250, TimeUnit.MILLISECONDS)")
                && clientResultSource.contains("stateNotifications: List<GameStateNotify>")
                && clientResultSource.contains("playerJoinedNotifications: List<PlayerJoinedNotify>")
                && clientResultSource.contains("finalState")
                && clientResultSource.contains("notifications:")
                && !clientResultSource.contains("pushes")
                && !clientResultSource.contains("GameWon"),
            "Kotlin TicTacToe direct client must subscribe to typed stream notifications and report actual received packets");
        assertFalse(clientSource.contains("systems.zlink.samples.kotlin.tictactoe.server."),
            "Kotlin TicTacToe Client role must not import server implementation");
        assertFalse(clientSource.contains("TicTacToeGameDirectory"),
            "Kotlin TicTacToe Client role must not access server game storage directly");
        assertTrue(apiSource.contains(".addClientServerChannel(")
                && apiSource.contains("settings.apiChannelEndpoint")
                && apiSource.contains("settings.playChannelEndpoint")
                && apiSource.contains("addHandlersFromPackageOf")
                && apiSource.contains("addHandlerGroup(\"api\")")
                && !apiSource.contains("addRequestHandler"),
            "Kotlin TicTacToe direct sample must expose the Api server role through annotation-discovered handlers");
        assertTrue(authHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && authHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayer\")")
                && createGameHandlerSource.contains("@RequestBody")
                && createGameHandlerSource.contains("CreateGameHttpReq")
                && createGameHandlerSource.contains("CreateGameHttpRes"),
            "Kotlin TicTacToe direct Api handlers must use annotation-based auth and HTTP create-game mapping");
        assertTrue(playSource.contains(".addSpotMesh("),
            "Kotlin TicTacToe direct sample must expose the Play Spot role");
        assertTrue(playSource.contains("addHandlersFromPackageOf")
                && playSource.contains("settings.apiChannelEndpoint")
                && playSource.contains("settings.playChannelEndpoint")
                && playSource.contains("settings.playRouterEndpoint")
                && playSource.contains("settings.playEndpoint")
                && playSource.contains("addHandlerGroup(SampleNames.PlayChannel)"),
            "Kotlin TicTacToe Play role must expose annotation-discovered play channel handlers");
        String playCreateGameHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/handlers/CreateGameHandler.kt");
        assertTrue(playCreateGameHandlerSource.contains("CompletionStage<CreateGameRes>")
                && playCreateGameHandlerSource.contains("ZLinkSpotManager")
                && playCreateGameHandlerSource.contains("@ZLinkHandlerGroup(SampleNames.PlayChannel)")
                && playCreateGameHandlerSource.contains("request: CreateGameReq")
                && playCreateGameHandlerSource.contains("spots.createAsync(TicTacToeGame::class.java)")
                && playCreateGameHandlerSource.contains("CreateGameRes("),
            "Kotlin TicTacToe Play CreateGame handler must create a Spot and reply with typed contracts");
        assertTrue(clientSource.contains("JoinGameReq(game.gameId)")
                && !clientSource.contains("TicTacToeGameJoinReq"),
            "Kotlin TicTacToe client must use client-facing JoinGame contracts only");
        String playJoinHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameJoinHandler.kt");
        String playActorJoinHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/entryspot/handlers/PlayActorJoinGameHandler.kt");
        String playPlaceMarkHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/PlayActorPlaceMarkHandler.kt");
        String gameCreatedHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameCreatedHandler.kt");
        String gameActorJoinedHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameActorJoinedHandler.kt");
        String gameActorLeftHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameActorLeftHandler.kt");
        String gameTimerHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/gamespots/handlers/TicTacToeGameTimerHandler.kt");
        String entryActorJoinedHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/entryspot/handlers/PlayEntrySpotActorJoinedHandler.kt");
        String entryActorLeftHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/entryspot/handlers/PlayEntrySpotActorLeftHandler.kt");
        assertTrue(playSessionSource.contains("ZLinkStreamJson.decode")
                && playSessionSource.contains("AuthenticatePlayerReq(request.accessToken)")
                && playSessionSource.contains("actorId = authenticatedActorId")
                && playSessionSource.contains("context.actors()::bindAsync")
                && playSessionSource.contains("requireActor().relayAsync(header, payload)")
                && !playSessionSource.contains("joinSpot(RoutingId.fromHex")
                && !playSessionSource.contains("split(\"|\")"),
            "Kotlin TicTacToe Play stream session must authenticate through the Api role and relay actor packets");
        assertTrue(playJoinHandlerSource.contains("CompletionStage<TicTacToeGameJoinRes>")
                && playJoinHandlerSource.contains("request: TicTacToeGameJoinReq")
                && playJoinHandlerSource.contains("TicTacToeGameJoinRes")
                && playActorJoinHandlerSource.contains("TicTacToeGameJoinReq")
                && playActorJoinHandlerSource.contains("JoinGameRes(result.reply().state)")
                && playPlaceMarkHandlerSource.contains("request: PlaceMarkReq"),
            "Kotlin TicTacToe Play actor handlers must receive typed stream request contracts");
        assertTrue(gameSpotSource.contains("actor.joinGame")
                && gameSpotSource.contains("boundSession()")
                && gameSpotSource.contains(".send(GameStateNotify")
                && gameSpotSource.contains(".send(message)"),
            "Kotlin TicTacToe game Spot must own joined-game state transitions and typed bound-session notifications");
        assertTrue(gameSpotSource.contains("onInitializeAsync()")
                && gameSpotSource.contains("context.addTimer(")
                && gameSpotSource.contains("TicTacToeGameTimerHandler::class.java")
                && gameSpotSource.contains("onClosingAsync()")
                && gameSpotSource.contains("gameTick?.cancelAsync()")
                && gameSpotSource.contains("TurnTimedOut")
                && gameSpotSource.contains("resetTurnDeadline()")
                && gameSpotSource.contains("tickAsync()")
                && gameSpotSource.contains("fun markCreated(createParts: List<Message>)")
                && gameSpotSource.contains("ensureCreated()")
                && gameCreatedHandlerSource.contains("handleAsync(")
                && gameCreatedHandlerSource.contains("List<Message>")
                && gameCreatedHandlerSource.contains("game.markCreated(createParts)")
                && gameTimerHandlerSource.contains("ZLinkSpotTimerHandler<TicTacToeGame>")
                && gameTimerHandlerSource.contains("spot.tickAsync()"),
            "Kotlin TicTacToe game Spot must mirror the .NET lifecycle, timer, and turn-timeout API usage");
        assertTrue(gameActorJoinedHandlerSource.contains("@ZLinkSpotPostActorJoined")
                && gameActorJoinedHandlerSource.contains("ZLinkSpotActorChangeResult")
                && gameActorLeftHandlerSource.contains("@ZLinkSpotActorLeft")
                && gameActorLeftHandlerSource.contains("ZLinkSpotActorChangeResult")
                && entryActorJoinedHandlerSource.contains("@ZLinkSpotPostActorJoined")
                && entryActorLeftHandlerSource.contains("@ZLinkSpotActorLeft"),
            "Kotlin TicTacToe EntrySpot and GameSpot lifecycle handler files must use framework lifecycle annotations, not placeholder methods");
        assertTrue(playActorJoinHandlerSource.contains("request.gameId"),
            "Kotlin TicTacToe join handler must store the requested game id");
        assertTrue(playPlaceMarkHandlerSource.contains("actor.requireJoinedGame()"),
            "Kotlin TicTacToe place handler must require actor join state");
        assertTrue(playActorSource.contains("joinedGameId"),
            "Kotlin TicTacToe PlayActor must own joined game state");
        assertTrue(playActorSource.contains("fun joinGame"),
            "Kotlin TicTacToe PlayActor must expose joinGame state transition");
        assertTrue(playActorSource.contains("fun requireJoinedGame"),
            "Kotlin TicTacToe PlayActor must validate joined game state");
        assertTrue(entrySpotSource.contains("context: ZLinkEntrySpotContext")
                && !entrySpotSource.contains("ZLinkEntrySpotContext? = null")
                && gameSpotSource.contains("private val context: ZLinkSpotContext")
                && !gameSpotSource.contains("constructor()")
                && !gameSpotSource.contains("SampleSpotContext")
                && !gameSpotSource.contains("CompletedSpotOutbound")
                && !gameSpotSource.contains("TicTacToeGameDirectory"),
            "Kotlin TicTacToe Spot instances must be created by the framework runtime, not sample-owned fallback contexts");
        assertTrue(playSource.contains(".addStreamNode("),
            "Kotlin TicTacToe direct sample must register the STREAM entry point");
        assertFalse(mainSource.contains("CreateGameHandler"),
            "Kotlin TicTacToe main must not collapse Play handler wiring into the entry point");
    }

    @Test
    void bingoMirrorsFourClientMatchingTimerAndBoundPushGate() throws IOException {
        assertSampleFilesExist("java", "Bingo", "src/main/java", List.of(
            "systems/zlink/samples/bingo/BingoSample.java"));
        assertNoSampleSourcesUnder("java", "Bingo", "src/main/java", List.of(
            "systems/zlink/samples/bingo/client",
            "systems/zlink/samples/bingo/server",
            "systems/zlink/samples/bingo/shared"));
        assertSampleFilesExist("java", "Bingo", "Client/src/main/java", List.of(
            "systems/zlink/samples/bingo/client/Program.java",
            "systems/zlink/samples/bingo/client/BingoClientApp.java",
            "systems/zlink/samples/bingo/client/BingoClientOptions.java",
            "systems/zlink/samples/bingo/client/BingoNotificationInbox.java",
            "systems/zlink/samples/bingo/client/BingoPlayerClient.java",
            "systems/zlink/samples/bingo/client/SampleAsync.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Api/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/api/Program.java",
            "systems/zlink/samples/bingo/server/api/ApiServerHostFactory.java",
            "systems/zlink/samples/bingo/server/api/handlers/AuthenticatePlayerHandler.java",
            "systems/zlink/samples/bingo/server/api/handlers/MatchBingoHandler.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Play/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/play/Program.java",
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
            "systems/zlink/samples/bingo/server/play/handlers/EnsurePlayerActorHandler.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Registry/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/registry/Program.java",
            "systems/zlink/samples/bingo/server/registry/RegistryHostFactory.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Session/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/session/Program.java",
            "systems/zlink/samples/bingo/server/session/SessionServerHostFactory.java",
            "systems/zlink/samples/bingo/server/session/sessions/BingoSession.java",
            "systems/zlink/samples/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.java"));
        assertSampleFilesExist("java", "Bingo", "Shared/src/main/java", List.of(
            "systems/zlink/samples/bingo/shared/configuration/SampleNames.java",
            "systems/zlink/samples/bingo/shared/configuration/SampleTopology.java",
            "systems/zlink/samples/bingo/shared/contracts/Messages.java"));

        String mainSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/BingoSample.java");
        String clientSource = sampleJavaSource(
            "Bingo",
            "Client/src/main/java",
            "systems/zlink/samples/bingo/client/BingoPlayerClient.java");
        String roomSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoRoomSpot.java");
        String publisherSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoNotificationPublisher.java");
        String apiHostSource = sampleJavaSource(
            "Bingo",
            "Server/Api/src/main/java",
            "systems/zlink/samples/bingo/server/api/ApiServerHostFactory.java");
        String playHostSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/PlayServerHostFactory.java");
        String registryHostSource = sampleJavaSource(
            "Bingo",
            "Server/Registry/src/main/java",
            "systems/zlink/samples/bingo/server/registry/RegistryHostFactory.java");
        String sessionHostSource = sampleJavaSource(
            "Bingo",
            "Server/Session/src/main/java",
            "systems/zlink/samples/bingo/server/session/SessionServerHostFactory.java");
        String apiProgramSource = sampleJavaSource(
            "Bingo",
            "Server/Api/src/main/java",
            "systems/zlink/samples/bingo/server/api/Program.java");
        String playProgramSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/Program.java");
        String registryProgramSource = sampleJavaSource(
            "Bingo",
            "Server/Registry/src/main/java",
            "systems/zlink/samples/bingo/server/registry/Program.java");
        String sessionProgramSource = sampleJavaSource(
            "Bingo",
            "Server/Session/src/main/java",
            "systems/zlink/samples/bingo/server/session/Program.java");
        String apiHandlerSource = sampleJavaSource(
            "Bingo",
            "Server/Api/src/main/java",
            "systems/zlink/samples/bingo/server/api/handlers/AuthenticatePlayerHandler.java");
        String playHandlerSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/handlers/EnsurePlayerActorHandler.java");

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
        assertTrue(clientSource.contains("BingoWinner")
                && !clientSource.contains("server.play")
                && !publisherSource.contains("BingoWinnerSink"),
            "Bingo sample must not couple client notification handling to server implementation types");
        assertTrue(apiHostSource.contains("addHandlersFromPackageOf")
                && apiHostSource.contains("addHandlerGroup(\"api\")")
                && !apiHostSource.contains("addRequestHandler")
                && playHostSource.contains("addHandlersFromPackageOf")
                && playHostSource.contains("addHandlerGroup(\"play\")")
                && !playHostSource.contains("addRequestHandler"),
            "Bingo Api/Play roles must use annotation-discovered handler groups");
        assertTrue(apiHostSource.contains("@SpringBootApplication")
                && apiHostSource.contains("SpringApplicationBuilder")
                && apiHostSource.contains("ZLinkFrameworkOptionsCustomizer")
                && !apiHostSource.contains("ZLinkFramework.start")
                && playHostSource.contains("@SpringBootApplication")
                && playHostSource.contains("SpringApplicationBuilder")
                && playHostSource.contains("ZLinkFrameworkOptionsCustomizer")
                && !playHostSource.contains("ZLinkFramework.start")
                && sessionHostSource.contains("@SpringBootApplication")
                && sessionHostSource.contains("SpringApplicationBuilder")
                && sessionHostSource.contains("ZLinkFrameworkOptionsCustomizer")
                && !sessionHostSource.contains("ZLinkFramework.start")
                && registryHostSource.contains("@SpringBootApplication")
                && registryHostSource.contains("SpringApplicationBuilder")
                && registryHostSource.contains("ZLinkEmbeddedRegistryOptions")
                && !registryHostSource.contains("ZLinkRegistry.start"),
            "Bingo server roles must run ZLink through Spring Boot lifecycle beans");
        assertFalse(apiProgramSource.contains("CountDownLatch")
                || playProgramSource.contains("CountDownLatch")
                || registryProgramSource.contains("CountDownLatch")
                || sessionProgramSource.contains("CountDownLatch"),
            "Bingo role entry points must not keep direct ZLink starts alive with CountDownLatch");
        assertTrue(apiHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && apiHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayer\")")
                && playHandlerSource.contains("@ZLinkHandlerGroup(\"play\")")
                && playHandlerSource.contains("@ZLinkRequest(packetName = \"EnsurePlayerActor\")"),
            "Bingo Api/Play handlers must use annotation-based request mapping");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Api/src/main/java",
                "systems/zlink/samples/bingo/server/api/Program.java", "ApiServerHostFactory.start"),
            "Java Bingo Api role must have its own executable Program");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Play/src/main/java",
                "systems/zlink/samples/bingo/server/play/Program.java", "PlayServerHostFactory.start"),
            "Java Bingo Play role must have its own executable Program");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Session/src/main/java",
                "systems/zlink/samples/bingo/server/session/Program.java", "SessionServerHostFactory.start"),
            "Java Bingo Session role must have its own executable Program");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Registry/src/main/java",
                "systems/zlink/samples/bingo/server/registry/Program.java", "RegistryHostFactory.start"),
            "Java Bingo Registry role must have its own executable Program");
        assertFalse(publisherSource.contains("BingoPlayerClient"),
            "Bingo server push must go through framework bound sessions, not direct client objects");
    }

    @Test
    void bingoKotlinSampleMirrorsJavaRoleLayout() throws IOException {
        assertSampleFilesExist("kotlin", "Bingo", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/BingoKotlinSample.kt"));
        assertNoSampleSourcesUnder("kotlin", "Bingo", "src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/client",
            "systems/zlink/samples/kotlin/bingo/server",
            "systems/zlink/samples/kotlin/bingo/shared"));
        assertSampleFilesExist("kotlin", "Bingo", "Client/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/client/Program.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoClientApp.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoClientOptions.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoNotificationInbox.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoPlayerClient.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Api/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/api/Program.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/ApiServerHostFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/AuthenticatePlayerHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/MatchBingoHandler.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Play/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/play/Program.kt",
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
            "systems/zlink/samples/kotlin/bingo/server/play/handlers/EnsurePlayerActorHandler.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Registry/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/registry/Program.kt",
            "systems/zlink/samples/kotlin/bingo/server/registry/RegistryHostFactory.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Session/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/session/Program.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/SessionServerHostFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/sessions/BingoSession.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Shared/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/shared/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/bingo/shared/configuration/SampleTopology.kt",
            "systems/zlink/samples/kotlin/bingo/shared/contracts/Messages.kt"));

        String mainSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/BingoKotlinSample.kt");
        String clientSource = sampleKotlinSource(
            "Bingo",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/client/BingoPlayerClient.kt");
        String roomSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoRoomSpot.kt");
        String publisherSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoNotificationPublisher.kt");
        String apiHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Api/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/api/ApiServerHostFactory.kt");
        String playHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/PlayServerHostFactory.kt");
        String registryHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Registry/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/registry/RegistryHostFactory.kt");
        String sessionHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/session/SessionServerHostFactory.kt");
        String apiProgramSource = sampleKotlinSource(
            "Bingo",
            "Server/Api/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/api/Program.kt");
        String playProgramSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/Program.kt");
        String registryProgramSource = sampleKotlinSource(
            "Bingo",
            "Server/Registry/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/registry/Program.kt");
        String sessionProgramSource = sampleKotlinSource(
            "Bingo",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/session/Program.kt");
        String apiHandlerSource = sampleKotlinSource(
            "Bingo",
            "Server/Api/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/AuthenticatePlayerHandler.kt");
        String playHandlerSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/handlers/EnsurePlayerActorHandler.kt");

        assertTrue(mainSource.contains("BingoClientOptions(4)")
                || mainSource.contains("BingoClientOptions(playerCount = 4)"),
            "Kotlin Bingo sample must create four connector clients");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "Kotlin Bingo sample must use connector public factory");
        assertTrue(clientSource.contains("ZLinkStreamDispatchMode.MANUAL"),
            "Kotlin Bingo sample must verify manual dispatch connector path");
        assertTrue(mainSource.contains("listOf(7, 11, 42, 42)"),
            "Kotlin Bingo sample must use deterministic draw sequence");
        assertTrue(roomSource.contains("\"player-2\", \"player-3\""),
            "Kotlin Bingo sample must verify same-sequence winners");
        assertTrue(clientSource.contains("BingoWinner")
                && !clientSource.contains("server.play")
                && !publisherSource.contains("BingoWinnerSink"),
            "Kotlin Bingo sample must not couple client notification handling to server implementation types");
        assertTrue(apiHostSource.contains("addHandlersFromPackageOf")
                && apiHostSource.contains("addHandlerGroup(\"api\")")
                && !apiHostSource.contains("addRequestHandler")
                && playHostSource.contains("addHandlersFromPackageOf")
                && playHostSource.contains("addHandlerGroup(\"play\")")
                && !playHostSource.contains("addRequestHandler"),
            "Kotlin Bingo Api/Play roles must use annotation-discovered handler groups");
        assertTrue(apiHostSource.contains("@SpringBootApplication")
                && apiHostSource.contains("SpringApplicationBuilder")
                && apiHostSource.contains("ZLinkFrameworkOptionsCustomizer")
                && !apiHostSource.contains("ZLinkFramework.start")
                && playHostSource.contains("@SpringBootApplication")
                && playHostSource.contains("SpringApplicationBuilder")
                && playHostSource.contains("ZLinkFrameworkOptionsCustomizer")
                && !playHostSource.contains("ZLinkFramework.start")
                && sessionHostSource.contains("@SpringBootApplication")
                && sessionHostSource.contains("SpringApplicationBuilder")
                && sessionHostSource.contains("ZLinkFrameworkOptionsCustomizer")
                && !sessionHostSource.contains("ZLinkFramework.start")
                && registryHostSource.contains("@SpringBootApplication")
                && registryHostSource.contains("SpringApplicationBuilder")
                && registryHostSource.contains("ZLinkEmbeddedRegistryOptions")
                && !registryHostSource.contains("ZLinkRegistry.start"),
            "Kotlin Bingo server roles must run ZLink through Spring Boot lifecycle beans");
        assertFalse(apiProgramSource.contains("CountDownLatch")
                || playProgramSource.contains("CountDownLatch")
                || registryProgramSource.contains("CountDownLatch")
                || sessionProgramSource.contains("CountDownLatch"),
            "Kotlin Bingo role entry points must not keep direct ZLink starts alive with CountDownLatch");
        assertTrue(apiHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && apiHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayer\")")
                && playHandlerSource.contains("@ZLinkHandlerGroup(\"play\")")
                && playHandlerSource.contains("@ZLinkRequest(packetName = \"EnsurePlayerActor\")"),
            "Kotlin Bingo Api/Play handlers must use annotation-based request mapping");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Api/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/api/Program.kt", "ApiServerHostFactory.start"),
            "Kotlin Bingo Api role must have its own executable Program");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Play/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/play/Program.kt", "PlayServerHostFactory.start"),
            "Kotlin Bingo Play role must have its own executable Program");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Session/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/session/Program.kt", "SessionServerHostFactory.start"),
            "Kotlin Bingo Session role must have its own executable Program");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Registry/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/registry/Program.kt", "RegistryHostFactory.start"),
            "Kotlin Bingo Registry role must have its own executable Program");
        assertFalse(publisherSource.contains("BingoPlayerClient"),
            "Kotlin Bingo server push must go through framework bound sessions, not direct client objects");
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
        assertTrue(source.contains("awaitPendingDispatch(connector)")
                && source.contains("pendingDispatchCount() > 0"),
            "StreamingClient sample must wait for manual queueing without sleep");
        assertTrue(source.contains("onConnectionStateChanged"),
            "StreamingClient sample must observe state changes");
        assertTrue(source.contains("onDisconnected"),
            "StreamingClient sample must observe disconnect");
        assertTrue(source.contains("disconnectAsync()"),
            "StreamingClient sample must simulate transport disconnect");
        assertTrue(source.contains("reconnectAsync()"),
            "StreamingClient sample must reconnect the same connector");
    }

    @Test
    void streamingClientKotlinMirrorsConnectorSmokeGate() throws IOException {
        String source = sampleKotlinSource(
            "StreamingClient",
            "systems/zlink/samples/kotlin/streamingclient/StreamingClientKotlinSample.kt");

        assertTrue(source.contains("ZLinkStreamDispatchMode.MANUAL"),
            "Kotlin StreamingClient sample must use manual dispatch");
        assertTrue(source.contains(".send("),
            "Kotlin StreamingClient sample must call send");
        assertTrue(source.contains(".request("),
            "Kotlin StreamingClient sample must call request");
        assertTrue(source.contains(".on("),
            "Kotlin StreamingClient sample must register handler");
        assertTrue(source.contains("awaitPendingDispatch(connector)")
                && source.contains("pendingDispatchCount() > 0"),
            "Kotlin StreamingClient sample must wait for manual queueing without sleep");
        assertTrue(source.contains("onConnectionStateChanged"),
            "Kotlin StreamingClient sample must observe state changes");
        assertTrue(source.contains("onDisconnected"),
            "Kotlin StreamingClient sample must observe disconnect");
        assertTrue(source.contains(".disconnect()"),
            "Kotlin StreamingClient sample must simulate transport disconnect");
        assertTrue(source.contains(".reconnect()"),
            "Kotlin StreamingClient sample must reconnect the same connector");
    }

    private static Path samplesRoot() {
        return frameworkJavaRoot().resolve("samples");
    }

    private static Path frameworkJavaRoot() {
        return Path.of(System.getProperty("user.dir")).getParent();
    }

    private static String sampleJavaSource(String sample, String relativePath) throws IOException {
        return sampleJavaSource(sample, "src/main/java", relativePath);
    }

    private static String sampleJavaSource(
        String sample,
        String sourceRoot,
        String relativePath) throws IOException {
        return Files.readString(samplesRoot()
            .resolve("java")
            .resolve(sample)
            .resolve(sourceRoot)
            .resolve(relativePath));
    }

    private static String sampleKotlinSource(String sample, String relativePath) throws IOException {
        return sampleKotlinSource(sample, "src/main/kotlin", relativePath);
    }

    private static String sampleKotlinSource(
        String sample,
        String sourceRoot,
        String relativePath) throws IOException {
        return Files.readString(samplesRoot()
            .resolve("kotlin")
            .resolve(sample)
            .resolve(sourceRoot)
            .resolve(relativePath));
    }

    private static boolean sampleFileContains(
        String language,
        String sample,
        String sourceRoot,
        String relativePath,
        String needle) throws IOException {
        Path file = samplesRoot()
            .resolve(language)
            .resolve(sample)
            .resolve(sourceRoot)
            .resolve(relativePath);
        return Files.isRegularFile(file) && Files.readString(file).contains(needle);
    }

    private static String sampleFile(
        String language,
        String sample,
        String sourceRoot,
        String relativePath) throws IOException {
        return Files.readString(samplesRoot()
            .resolve(language)
            .resolve(sample)
            .resolve(sourceRoot)
            .resolve(relativePath));
    }

    private static void assertDotNetProjectLayout(Path sampleRoot, List<String> relativeProjects) {
        for (String relativeProject : relativeProjects) {
            Path projectRoot = sampleRoot.resolve(relativeProject);
            assertTrue(Files.isDirectory(projectRoot),
                "missing .NET-parity project folder " + projectRoot);
            assertTrue(Files.isRegularFile(projectRoot.resolve("build.gradle.kts")),
                "missing Gradle project for .NET-parity folder " + projectRoot);
        }
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

    private static void assertNoSampleSourcesUnder(
        String language,
        String sample,
        String sourceRoot,
        List<String> relativeDirectories) throws IOException {
        Path sampleSourceRoot = samplesRoot()
            .resolve(language)
            .resolve(sample)
            .resolve(sourceRoot);
        for (String relativeDirectory : relativeDirectories) {
            Path directory = sampleSourceRoot.resolve(relativeDirectory);
            if (!Files.exists(directory)) {
                continue;
            }
            try (var paths = Files.walk(directory)) {
                assertTrue(paths.noneMatch(SampleReleaseGateContractTest::isSampleSource),
                    "role source must not remain under aggregate source root: "
                        + language + "/" + sample + "/" + sourceRoot + "/" + relativeDirectory);
            }
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
