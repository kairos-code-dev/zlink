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
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorLeftHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/registry/RegistryServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/SessionServer.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/PlayerSession.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/PlayerSessionDirectory.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/AuthenticateSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/CreateMatchSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.java",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.java",
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
        String apiServerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/api/ApiServer.java");
        String clientSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchClient.java");
        String playerClientSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.java");
        String playerSessionSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/PlayerSession.java");
        String joinSessionHandlerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.java");
        String placeSessionHandlerSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.java");
        String relaySource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.java");
        String notificationPublisherSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/GameNotificationPublisher.java");
        String playDirectorySource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameDirectory.java");
        String playGameSpotSource = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameSpot.java");
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
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/entryspot/handlers/TicTacToeEntrySpotActorLeftHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/registry/RegistryServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/SessionServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/PlayerSession.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/PlayerSessionDirectory.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/AuthenticateSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/CreateMatchSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/actors/PlayerActor.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/actors/PlayerActorFactory.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/configuration/SampleTopology.kt",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/shared/contracts/Messages.kt"));

        String sessionServerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/SessionServer.kt");
        String mainSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/TicTacToeSessionGatewayKotlinSample.kt");
        String playServerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/PlayServer.kt");
        String apiServerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/api/ApiServer.kt");
        String clientSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchClient.kt");
        String playerClientSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/client/SessionActorDispatchPlayerClient.kt");
        String playerSessionSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/PlayerSession.kt");
        String joinSessionHandlerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/JoinMatchSessionPacketHandler.kt");
        String placeSessionHandlerSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlaceMarkSessionPacketHandler.kt");
        String relaySource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/session/sessions/handlers/PlayNotificationRelay.kt");
        String notificationPublisherSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/GameNotificationPublisher.kt");
        String playDirectorySource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameDirectory.kt");
        String playGameSpotSource = sampleKotlinSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/kotlin/tictactoe/sessiongateway/server/play/gamespots/TicTacToeGameSpot.kt");

        assertTrue(sessionServerSource.contains("attachActorGateway(SampleNames.SessionRelayNode)")
                || sessionServerSource.contains("attachActorGateway(\"session-relay\")"),
            "Kotlin SessionGateway sample must attach stream node to local ActorGateway SpotNode");
        assertTrue(mainSource.contains("ZLinkFramework.start"),
            "Kotlin SessionGateway sample must start the framework through the public facade");
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
        assertTrue(sampleFileContains("java", "TicTacToe", "Client/src/main/java",
                "systems/zlink/samples/tictactoe/client/Program.java", "TicTacToeClient"),
            "Java TicTacToe Client role Program must live in the Client project folder");
        assertTrue(sampleFileContains("java", "TicTacToe", "Server/src/main/java",
                "systems/zlink/samples/tictactoe/server/Program.java", "PlayServer::configure"),
            "Java TicTacToe Server role Program must live in the Server project folder");

        String mainSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/TicTacToeSample.java");
        String clientSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/client/TicTacToeClient.java");
        String apiSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/server/api/ApiServer.java");
        String authHandlerSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/server/api/handlers/AuthenticatePlayerHandler.java");
        String createGameHandlerSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/server/api/handlers/CreateGameHttpHandler.java");
        String playSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/server/play/PlayServer.java");

        assertTrue(mainSource.contains("ZLinkFramework.start"),
            "TicTacToe direct sample must start the framework through the public facade");
        assertTrue(mainSource.contains("options.codecs().addJson()")
                && apiSource.contains("options.codecs().addJson()")
                && playSource.contains("options.codecs().addJson()"),
            "TicTacToe direct Api, Play, and client framework hosts must enable JSON codecs for typed contracts");
        assertTrue(clientSource.contains(".requestToChannel("),
            "TicTacToe direct sample must use framework channel request path");
        assertTrue(clientSource.contains(".submitAsync(CreateGameRes.class)")
                && createGameHandlerSource.contains("CompletionStage<CreateGameRes>")
                && createGameHandlerSource.contains(".submitAsync(CreateGameRes.class)"),
            "TicTacToe direct CreateGame channel path must use the CreateGameRes contract object");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "TicTacToe Client role must use the public stream connector for play requests");
        assertFalse(clientSource.contains("systems.zlink.samples.tictactoe.server."),
            "TicTacToe Client role must not import server implementation");
        assertFalse(clientSource.contains("TicTacToeGameDirectory"),
            "TicTacToe Client role must not access server game storage directly");
        assertTrue(apiSource.contains(".addClientServerChannel(")
                && apiSource.contains("addHandlersFromPackageOf")
                && apiSource.contains("addHandlerGroup(\"api\")")
                && !apiSource.contains("addRequestHandler"),
            "TicTacToe direct sample must expose the Api server role through annotation-discovered handlers");
        assertTrue(authHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && authHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayer\")")
                && createGameHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && createGameHandlerSource.contains("@ZLinkRequest(packetName = \"CreateGame\")"),
            "TicTacToe direct Api handlers must use annotation-based request mapping");
        assertTrue(playSource.contains(".addSpotMesh("),
            "TicTacToe direct sample must expose the Play Spot role");
        assertTrue(playSource.contains("addHandlersFromPackageOf")
                && playSource.contains("addHandlerGroup(\"play\")"),
            "TicTacToe Play role must expose annotation-discovered play channel handlers");
        String playCreateGameHandlerSource = sampleJavaSource(
            "TicTacToe",
            "systems/zlink/samples/tictactoe/server/play/handlers/CreateGameHandler.java");
        assertTrue(playCreateGameHandlerSource.contains("CompletionStage<CreateGameRes>")
                && playCreateGameHandlerSource.contains("new CreateGameRes("),
            "TicTacToe Play CreateGame handler must reply with the typed CreateGameRes contract");
        assertTrue(playSource.contains(".addStreamNode("),
            "TicTacToe direct sample must register the STREAM entry point");
        assertFalse(mainSource.contains("CreateGameHandler"),
            "TicTacToe main must not collapse Play handler wiring into the entry point");
    }

    @Test
    void ticTacToeKotlinSampleMirrorsJavaRoleLayout() throws IOException {
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
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Client/src/main/kotlin",
                "systems/zlink/samples/kotlin/tictactoe/client/Program.kt", "TicTacToeClient"),
            "Kotlin TicTacToe Client role Program must live in the Client project folder");
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Server/src/main/kotlin",
                "systems/zlink/samples/kotlin/tictactoe/server/Program.kt", "PlayServer::configure"),
            "Kotlin TicTacToe Server role Program must live in the Server project folder");

        String mainSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/TicTacToeKotlinSample.kt");
        String clientSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClient.kt");
        String apiSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServer.kt");
        String authHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/AuthenticatePlayerHandler.kt");
        String createGameHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/CreateGameHttpHandler.kt");
        String playSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/server/play/PlayServer.kt");

        assertTrue(mainSource.contains("ZLinkFramework.start"),
            "Kotlin TicTacToe direct sample must start the framework through the public facade");
        assertTrue(mainSource.contains("options.codecs().addJson()")
                && apiSource.contains("options.codecs().addJson()")
                && playSource.contains("options.codecs().addJson()"),
            "Kotlin TicTacToe direct Api, Play, and client framework hosts must enable JSON codecs for typed contracts");
        assertTrue(clientSource.contains(".requestToChannel("),
            "Kotlin TicTacToe direct sample must use framework channel request path");
        assertTrue(clientSource.contains(".awaitReply<CreateGameRes>()")
                && createGameHandlerSource.contains("CompletionStage<CreateGameRes>")
                && createGameHandlerSource.contains(".submitAsync(CreateGameRes::class.java)"),
            "Kotlin TicTacToe direct CreateGame channel path must use the CreateGameRes contract object");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "Kotlin TicTacToe Client role must use the public stream connector for play requests");
        assertFalse(clientSource.contains("systems.zlink.samples.kotlin.tictactoe.server."),
            "Kotlin TicTacToe Client role must not import server implementation");
        assertFalse(clientSource.contains("TicTacToeGameDirectory"),
            "Kotlin TicTacToe Client role must not access server game storage directly");
        assertTrue(apiSource.contains(".addClientServerChannel(")
                && apiSource.contains("addHandlersFromPackageOf")
                && apiSource.contains("addHandlerGroup(\"api\")")
                && !apiSource.contains("addRequestHandler"),
            "Kotlin TicTacToe direct sample must expose the Api server role through annotation-discovered handlers");
        assertTrue(authHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && authHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayer\")")
                && createGameHandlerSource.contains("@ZLinkHandlerGroup(\"api\")")
                && createGameHandlerSource.contains("@ZLinkRequest(packetName = \"CreateGame\")"),
            "Kotlin TicTacToe direct Api handlers must use annotation-based request mapping");
        assertTrue(playSource.contains(".addSpotMesh("),
            "Kotlin TicTacToe direct sample must expose the Play Spot role");
        assertTrue(playSource.contains("addHandlersFromPackageOf")
                && playSource.contains("addHandlerGroup(\"play\")"),
            "Kotlin TicTacToe Play role must expose annotation-discovered play channel handlers");
        String playCreateGameHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "systems/zlink/samples/kotlin/tictactoe/server/play/handlers/CreateGameHandler.kt");
        assertTrue(playCreateGameHandlerSource.contains("CompletionStage<CreateGameRes>")
                && playCreateGameHandlerSource.contains("CreateGameRes("),
            "Kotlin TicTacToe Play CreateGame handler must reply with the typed CreateGameRes contract");
        assertTrue(playSource.contains(".addStreamNode("),
            "Kotlin TicTacToe direct sample must register the STREAM entry point");
        assertFalse(mainSource.contains("CreateGameHandler"),
            "Kotlin TicTacToe main must not collapse Play handler wiring into the entry point");
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
            "systems/zlink/samples/bingo/server/play/bingoroomspots/BingoWinnerSink.java",
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
        String apiHostSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/server/api/ApiServerHostFactory.java");
        String playHostSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/server/play/PlayServerHostFactory.java");
        String apiHandlerSource = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/server/api/handlers/AuthenticatePlayerHandler.java");
        String playHandlerSource = sampleJavaSource(
            "Bingo",
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
                && publisherSource.contains("BingoWinnerSink"),
            "Bingo sample must push winner notifications through a narrow sink port");
        assertTrue(apiHostSource.contains("addHandlersFromPackageOf")
                && apiHostSource.contains("addHandlerGroup(\"api\")")
                && !apiHostSource.contains("addRequestHandler")
                && playHostSource.contains("addHandlersFromPackageOf")
                && playHostSource.contains("addHandlerGroup(\"play\")")
                && !playHostSource.contains("addRequestHandler"),
            "Bingo Api/Play roles must use annotation-discovered handler groups");
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
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoWinnerSink.kt",
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

        String mainSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/BingoKotlinSample.kt");
        String clientSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/client/BingoPlayerClient.kt");
        String roomSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoRoomSpot.kt");
        String publisherSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/server/play/bingoroomspots/BingoNotificationPublisher.kt");
        String apiHostSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/server/api/ApiServerHostFactory.kt");
        String playHostSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/server/play/PlayServerHostFactory.kt");
        String apiHandlerSource = sampleKotlinSource(
            "Bingo",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/AuthenticatePlayerHandler.kt");
        String playHandlerSource = sampleKotlinSource(
            "Bingo",
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
                && publisherSource.contains("BingoWinnerSink"),
            "Kotlin Bingo sample must push winner notifications through a narrow sink port");
        assertTrue(apiHostSource.contains("addHandlersFromPackageOf")
                && apiHostSource.contains("addHandlerGroup(\"api\")")
                && !apiHostSource.contains("addRequestHandler")
                && playHostSource.contains("addHandlersFromPackageOf")
                && playHostSource.contains("addHandlerGroup(\"play\")")
                && !playHostSource.contains("addRequestHandler"),
            "Kotlin Bingo Api/Play roles must use annotation-discovered handler groups");
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
        return Files.readString(samplesRoot()
            .resolve("java")
            .resolve(sample)
            .resolve("src/main/java")
            .resolve(relativePath));
    }

    private static String sampleKotlinSource(String sample, String relativePath) throws IOException {
        return Files.readString(samplesRoot()
            .resolve("kotlin")
            .resolve(sample)
            .resolve("src/main/kotlin")
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
