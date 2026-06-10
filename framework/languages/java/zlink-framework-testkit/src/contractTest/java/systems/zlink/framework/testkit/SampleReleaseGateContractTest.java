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
    private static final String FORBIDDEN_SAMPLE_ASYNC_HELPER = "Sample" + "Async";
    private static final String FORBIDDEN_TICTACTOE_RESULT = "TicTacToeClient" + "Result";

    private static final Set<String> REQUIRED_SAMPLES = Set.of(
        "TicTacToe",
        "Bingo");

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

    private static final List<String> DOTNET_BINGO_SAMPLE_PROJECTS = List.of(
        "Client",
        "Server/Api",
        "Server/Play",
        "Server/Registry",
        "Server/Session",
        "Shared");

    private static final List<String> FORBIDDEN_SAMPLE_PATTERNS = List.of(
        "import systems.zlink.runtime.",
        "import systems.zlink.internal.",
        "systems.zlink.contracts.core.RoutingId.",
        "RouteStore",
        "MetadataStore",
        "RemoteAddressResolver",
        "BingoNotificationLoopbackServer",
        "BingoRoomState room",
        "new BingoRoomState",
        "CountDownLatch",
        "Thread.sleep",
        "sleep(",
        "System.in.read",
        "while (true)",
        "while(true)",
        "toCompletableFuture()",
        FORBIDDEN_SAMPLE_ASYNC_HELPER,
        "session relay JSON",
        "in-memory route channel replacement");

    @Test
    void requiredSamplesExposeExecutableEntryPoints() throws IOException {
        Path samplesRoot = samplesRoot();

        assertTrue(Files.isRegularFile(samplesRoot.resolve("run_samples.sh")),
            "missing aggregate sample runner");
        assertTrue(Files.isExecutable(samplesRoot.resolve("run_samples.sh")),
            "aggregate sample runner must be executable");
        String aggregateRunner = Files.readString(samplesRoot.resolve("run_samples.sh"));
        assertTrue(aggregateRunner.contains("ZLINK_LIBRARY_PATH")
                && aggregateRunner.contains("core/build/lib/libzlink.so"),
            "aggregate sample runner must use the local core runtime when it is available");

        for (String language : REQUIRED_LANGUAGES) {
            Path languageRoot = samplesRoot.resolve(language);
            assertTrue(Files.isDirectory(languageRoot), "missing sample language directory " + language);

            for (String sample : REQUIRED_SAMPLES) {
                Path sampleRoot = languageRoot.resolve(sample);
                String sampleName = language + "/" + sample;
                assertTrue(Files.isDirectory(sampleRoot), "missing sample " + sampleName);
                assertFalse(Files.exists(sampleRoot.resolve("settings.gradle.kts")),
                    "nested settings.gradle.kts makes IntelliJ import duplicate Gradle roots for " + sampleName);
                assertTrue(Files.isRegularFile(sampleRoot.resolve("standalone.settings.gradle.kts")),
                    "missing standalone.settings.gradle.kts for " + sampleName);
                assertTrue(Files.isRegularFile(sampleRoot.resolve("build.gradle.kts")),
                    "missing build.gradle.kts for " + sampleName);
                assertTrue(Files.isRegularFile(sampleRoot.resolve("run_sample.sh")),
                    "missing run_sample.sh for " + sampleName);
                assertTrue(Files.isExecutable(sampleRoot.resolve("run_sample.sh")),
                    "run_sample.sh must be executable for " + sampleName);
                String runner = Files.readString(sampleRoot.resolve("run_sample.sh"));
                assertTrue(runner.contains("--settings-file standalone.settings.gradle.kts"),
                    "run_sample.sh must use standalone settings for " + sampleName);
            }

            assertDotNetProjectLayout(languageRoot.resolve("TicTacToe"), DOTNET_DIRECT_SAMPLE_PROJECTS);
            assertDotNetProjectLayout(languageRoot.resolve("Bingo"), DOTNET_BINGO_SAMPLE_PROJECTS);
        }
    }

    @Test
    void roleBasedSamplesDoNotCollapseIntoSingleGradleRun() throws IOException {
        for (String language : REQUIRED_LANGUAGES) {
            for (String sample : List.of("TicTacToe", "Bingo")) {
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
    void serverRoleSamplesRunZLinkThroughSpringLifecycle() throws IOException {
        for (String language : REQUIRED_LANGUAGES) {
            for (String sample : List.of("TicTacToe", "Bingo")) {
                Path sampleRoot = samplesRoot().resolve(language).resolve(sample);
                try (Stream<Path> files = Files.walk(sampleRoot)) {
                    Map<Path, List<String>> directStarts = files
                        .filter(Files::isRegularFile)
                        .filter(SampleReleaseGateContractTest::isSampleSource)
                        .filter(SampleReleaseGateContractTest::isServerRoleSource)
                        .map(path -> Map.entry(path, forbiddenDirectServerStarts(path)))
                        .filter(entry -> !entry.getValue().isEmpty())
                        .collect(java.util.stream.Collectors.toMap(
                            Map.Entry::getKey,
                            Map.Entry::getValue));

                    assertTrue(directStarts.isEmpty(),
                        language + "/" + sample + " server roles must not start ZLink directly: "
                            + directStarts);
                }

                try (Stream<Path> files = Files.walk(sampleRoot)) {
                    List<Path> nonSpringHosts = files
                        .filter(Files::isRegularFile)
                        .filter(SampleReleaseGateContractTest::isSampleSource)
                        .filter(SampleReleaseGateContractTest::isServerRoleSource)
                        .filter(path -> path.getFileName().toString().contains("HostFactory"))
                        .filter(SampleReleaseGateContractTest::isNotSpringBootHostFactory)
                        .toList();

                    assertTrue(nonSpringHosts.isEmpty(),
                        language + "/" + sample
                            + " server role Application classes must use Spring Boot lifecycle: "
                            + nonSpringHosts);
                }
            }
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
    void ticTacToeDirectSampleUsesFrameworkRuntimePublicFacade() throws IOException {
        assertNoSampleSourcesUnder("java", "TicTacToe", "src/main/java",
            List.of(
                "systems/zlink/samples/tictactoe/client",
                "systems/zlink/samples/tictactoe/server",
                "systems/zlink/samples/tictactoe/shared"));
        assertSampleFilesExist("java", "TicTacToe", "Client/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/client/TicTacToeClientArguments.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClient.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeClientOptions.java",
            "systems/zlink/samples/tictactoe/client/TicTacToeSampleDefaults.java"));
        assertTrue(sampleFileContains("java", "TicTacToe", "Client/src/main/java",
                "systems/zlink/samples/tictactoe/client/Program.java", "tictactoe=completed"),
            "Java TicTacToe Client role must report the same completion marker as other clients");
        assertTrue(sampleFileContains("java", "TicTacToe", "Client",
                "README.md", "Tic Tac Toe Client"),
            "Java TicTacToe Client role must include a standalone README");
        assertSampleFilesExist("java", "TicTacToe", "Server/src/main/java", List.of(
            "systems/zlink/samples/tictactoe/server/api/ApiServer.java",
            "systems/zlink/samples/tictactoe/server/api/ApiServerApplication.java",
            "systems/zlink/samples/tictactoe/server/api/handlers/AuthenticatePlayerHandler.java",
            "systems/zlink/samples/tictactoe/server/api/handlers/CreateGameHttpHandler.java",
            "systems/zlink/samples/tictactoe/server/TicTacToeServerApplicationGroup.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleLogging.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleNames.java",
            "systems/zlink/samples/tictactoe/server/configuration/SamplePorts.java",
            "systems/zlink/samples/tictactoe/server/configuration/SampleSettings.java",
            "systems/zlink/samples/tictactoe/server/play/PlayServer.java",
            "systems/zlink/samples/tictactoe/server/play/PlayServerApplication.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/actors/PlayActor.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/actors/PlayActorFactory.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/PlayEntrySpot.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/TicTacToeGame.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorPlaceMarkHandler.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/handlers/TicTacToeGameTimerHandler.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/handlers/CreateGameHandler.java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/sessions/PlaySession.java"));
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
                "systems/zlink/samples/tictactoe/server/Program.java", "PlayServerApplication.run(settings)"),
            "Java TicTacToe Server role Program must live in the Server project folder");

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
        String clientProgramSource = sampleJavaSource(
            "TicTacToe",
            "Client/src/main/java",
            "systems/zlink/samples/tictactoe/client/Program.java");
        String apiSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/api/ApiServer.java");
        String apiHostFactorySource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/api/ApiServerApplication.java");
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
        String playHostFactorySource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/PlayServerApplication.java");
        String playActorSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/actors/PlayActor.java");
        String entrySpotSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/PlayEntrySpot.java");
        String gameSpotSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/TicTacToeGame.java");
        String playSessionSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/sessions/PlaySession.java");

        assertTrue(serverProgramSource.contains("case \"api\" -> ApiServerApplication.run(settings)")
                && serverProgramSource.contains("case \"play\" -> PlayServerApplication.run(settings)")
                && serverProgramSource.contains("case \"all\" -> runAll(settings)")
                && serverProgramSource.contains("case \"client\" -> runClient(settings)")
                && serverProgramSource.contains("SampleSettings.fromArgs(args)")
                && serverProgramSource.contains("TicTacToeServerApplicationGroup.run(effectiveSettings)")
                && !serverProgramSource.contains("case \"server\"")
                && serverBuildSource.contains("implementation(sampleProject(\"Client\"))"),
            "TicTacToe Java Server Program must expose .NET-style all/play/api/client modes");
        assertFalse(serverProgramSource.contains("CountDownLatch")
                || serverProgramSource.contains("ZLinkFramework.start"),
            "TicTacToe Java Server roles must rely on Spring lifecycle keep-alive instead of direct framework execution");
        assertTrue(apiSource.contains("ZLinkFrameworkConfigurer")
                && apiSource.contains("options.codecs().addMessagePack()")
                && playSource.contains("ZLinkFrameworkConfigurer")
                && playSource.contains("options.codecs().addMessagePack()")
                && apiHostFactorySource.contains("@SpringBootApplication")
                && apiHostFactorySource.contains("SpringApplicationBuilder")
                && apiHostFactorySource.contains(".web(WebApplicationType.SERVLET)")
                && apiHostFactorySource.contains("setKeepAlive(true)")
                && apiHostFactorySource.contains("ApiServer.configure(settings)")
                && playHostFactorySource.contains("@SpringBootApplication")
                && playHostFactorySource.contains("SpringApplicationBuilder")
                && playHostFactorySource.contains(".web(WebApplicationType.NONE)")
                && playHostFactorySource.contains("setKeepAlive(true)")
                && playHostFactorySource.contains("PlayServer.configure(settings)"),
            "TicTacToe direct Api and Play framework hosts must enable MessagePack codecs and expose HTTP create-game with shared settings");
        assertTrue(settingsSource.contains("withEphemeralDefaults()")
                && settingsSource.contains("SamplePorts.reserve()")
                && settingsSource.contains("--api-bind")
                && settingsSource.contains("--api-url")
                && settingsSource.contains("--api-channel-endpoint")
                && settingsSource.contains("--play-channel-endpoint")
                && settingsSource.contains("--play-router-endpoint")
                && settingsSource.contains("--play-endpoint")
                && !settingsSource.contains("static SampleSettings current")
                && !settingsSource.contains("current()")
                && !settingsSource.contains("setCurrent"),
            "TicTacToe direct sample must expose .NET-style sample settings through Spring DI instead of fixed topology constants or global state");
        assertTrue(clientSource.contains("HttpClient")
                && clientSource.contains("CreateGameHttpReq")
                && clientSource.contains("CreateGameHttpRes")
                && clientSource.contains(".resolve(\"/games\")")
                && !clientSource.contains(".requestToChannel("),
            "TicTacToe direct client must create games through the HTTP API path");
        String playAuthHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/sessions/handlers/AuthenticatePlaySessionHandler.java");
        assertTrue(playAuthHandlerSource.contains("new AuthenticatePlayerReq(request.accessToken())")
                && playAuthHandlerSource.contains(".submit(AuthenticatePlayerRes.class)")
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
                && createGameHandlerSource.contains(".timeout(SampleNames.RequestTimeout)")
                && createGameHandlerSource.contains(".submit(CreateGameRes.class)")
                && !createGameHandlerSource.contains(".packetName(\"CreateGameReq\")")
                && !createGameHandlerSource.contains("HttpExchange")
                && !createGameHandlerSource.contains("HttpServer"),
            "TicTacToe HTTP create-game endpoint must run as a Spring controller and translate to the typed Play channel request");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "TicTacToe Client role must use the public stream connector for play requests");
        assertTrue(clientSource.contains("new AuthenticateReq(options.xActorId())")
                && clientSource.contains("new JoinGameReq(game.roomId())")
                && clientSource.contains("new PlaceMarkReq(3)")
                && clientSource.contains("new PlaceMarkReq(4)")
                && clientSource.contains("new PlaceMarkReq(2)")
                && clientSource.contains("URI.create(endpoint)")
                && clientSource.contains("public void run(TicTacToeClientOptions options) throws Exception")
                && clientSource.contains(".request(new AuthenticateReq(options.xActorId()))")
                && clientSource.contains(".await(AuthenticateRes.class)")
                && clientSource.contains(".request(new PlaceMarkReq(2))")
                && clientSource.contains(".await(PlaceMarkRes.class)")
                && clientSource.contains("\"Won\".equals(hostWin.state().status())")
                && clientSource.contains("options.xActorId().equals(hostWin.state().winner())")
                && clientSource.contains("host.close().await()")
                && clientSource.contains("guest.close().await()")
                && clientSource.contains("ZLinkStreamMessagePack.codec()")
                && !clientSource.contains("ZLinkStreamMessagePack.request")
                && !clientSource.contains(FORBIDDEN_TICTACTOE_RESULT)
                && !clientSource.contains("requestStep(")
                && !clientSource.contains("validateFinalState(")
                && !clientSource.contains("thenCompose(")
                && !clientSource.contains("game.gameId() + \"|\""),
            "TicTacToe stream client path must use connector member request contracts and assert the .NET winning scenario");
        assertTrue(clientSource.contains(".waitFor(PlayerJoinedNotify.class)")
                && clientSource.contains(".waitFor(GameStateNotify.class)")
                && clientSource.contains("ZLinkStreamDispatchMode.AUTO")
                && clientSource.contains(".where(PlayerJoinedNotify.class,")
                && clientSource.contains(".where(GameStateNotify.class,")
                && clientSource.contains(".submit(PlayerJoinedNotify.class)")
                && clientSource.contains(".submit(GameStateNotify.class)")
                && clientSource.contains("hostSawGuestJoin")
                && clientSource.contains("hostSawGameStart")
                && clientSource.contains("guestSawHostWin")
                && clientProgramSource.contains("new TicTacToeClient().run(clientOptions)")
                && !clientProgramSource.contains("awaitSample(")
                && !clientSource.contains("ZLinkStreamMessagePack.on")
                && !clientSource.contains("ZLinkStreamMessagePack.waitForAsync")
                && !clientSource.contains("ConcurrentLinkedQueue")
                && !clientSource.contains("stateNotifications")
                && !clientSource.contains("playerJoinedNotifications")
                && clientProgramSource.contains("tictactoe=completed")
                && !clientProgramSource.contains("writeTo(System.out)"),
            "TicTacToe direct client must use fluent wait and avoid returning a result DTO");
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
                && authHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayerReq\")")
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
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/handlers/CreateGameHandler.java");
        assertTrue(playCreateGameHandlerSource.contains("CompletionStage<CreateGameRes>")
                && playCreateGameHandlerSource.contains("ZLinkSpotManager")
                && playCreateGameHandlerSource.contains("SampleSettings settings")
                && playCreateGameHandlerSource.contains("@ZLinkHandlerGroup(SampleNames.PlayChannel)")
                && playCreateGameHandlerSource.contains("CreateGameReq request")
                && playCreateGameHandlerSource.contains("gameCreator.nextRoom(request.gameName())")
                && playCreateGameHandlerSource.contains("RoutingId.from(room.roomId())")
                && playCreateGameHandlerSource.contains("new CreateGameRes(")
                && !playCreateGameHandlerSource.contains("SampleSettings.current()"),
            "TicTacToe Play CreateGame handler must be created by Spring DI, create a Spot, and reply with typed contracts");
        String gameJoinReqSource = sampleJavaSource(
            "TicTacToe",
            "Shared/src/main/java",
            "systems/zlink/samples/tictactoe/shared/contracts/TicTacToeGameJoinReq.java");
        String gameJoinResSource = sampleJavaSource(
            "TicTacToe",
            "Shared/src/main/java",
            "systems/zlink/samples/tictactoe/shared/contracts/TicTacToeGameJoinRes.java");
        String playActorJoinHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorJoinGameHandler.java");
        String playPlaceMarkHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorPlaceMarkHandler.java");
        String gameCreatedHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/handlers/TicTacToeGameCreatedHandler.java");
        String gameTimerHandlerSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/handlers/TicTacToeGameTimerHandler.java");
        String matchDomainSource = sampleJavaSource(
            "TicTacToe",
            "Server/src/main/java",
            "systems/zlink/samples/tictactoe/server/play/domain/tictactoe/TicTacToeMatch.java");
        assertTrue(playAuthHandlerSource.contains("ZLinkStreamMessagePack.decode")
                && playAuthHandlerSource.contains("new AuthenticatePlayerReq(request.accessToken())")
                && playAuthHandlerSource.contains("context.actors()::bindAsync")
                && playSessionSource.contains("handlers.tryHandleAsync(context, header, payload)")
                && playSessionSource.contains("requireActor(header.packetName()).relayAsync(header, payload)")
                && !playSessionSource.contains("joinEntrySpot(")
                && !playSessionSource.contains("joinSpot(RoutingId.fromHex")
                && !playSessionSource.contains("split(\"\\\\|\")"),
            "TicTacToe Play stream session must authenticate through the Api role and relay actor packets");
        assertTrue(gameJoinReqSource.contains("String roomId")
                && gameJoinReqSource.contains("String actorId")
                && gameJoinResSource.contains("GameState state"),
            "TicTacToe direct sample must split client JoinGame contracts from Spot join contracts");
        assertTrue(gameSpotSource.contains("onActorJoinAsync(")
                && gameSpotSource.contains("Message request")
                && gameSpotSource.contains("ZLinkSpotActorJoinResponse.accept")
                && gameSpotSource.contains("TicTacToeGameJoinReq.class")
                && gameSpotSource.contains("TicTacToeGameJoinRes")
                && playActorJoinHandlerSource.contains("new TicTacToeGameJoinReq")
                && playActorJoinHandlerSource.contains("PlayEntrySpot entrySpot")
                && playActorJoinHandlerSource.contains("ZLinkSpotActorRequestContext context")
                && playActorJoinHandlerSource.contains("CancellationToken cancellationToken")
                && playActorJoinHandlerSource.contains("new JoinGameRes")
                && playPlaceMarkHandlerSource.contains("TicTacToeGame spot")
                && playPlaceMarkHandlerSource.contains("ZLinkSpotActorRequestContext context")
                && playPlaceMarkHandlerSource.contains("PlaceMarkReq request")
                && playPlaceMarkHandlerSource.contains("CancellationToken cancellationToken"),
            "TicTacToe Play actor join admission must be a Message-based Spot member callback while game packets stay typed");
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
                && matchDomainSource.contains("TurnTimedOut")
                && matchDomainSource.contains("resetTurnDeadline(")
                && gameSpotSource.contains("tickAsync()")
                && gameSpotSource.contains("onCreateAsync(Message request)")
                && gameSpotSource.contains("markCreated(Message request)")
                && gameSpotSource.contains("ensureCreated()")
                && gameCreatedHandlerSource.contains("handleAsync(")
                && gameCreatedHandlerSource.contains("Message request")
                && gameCreatedHandlerSource.contains("ZLinkSpotCreateResponse.accept()")
                && gameCreatedHandlerSource.contains("game.markCreated(request)")
                && gameTimerHandlerSource.contains("implements ZLinkSpotTimerHandler<TicTacToeGame>")
                && gameTimerHandlerSource.contains("spot.tickAsync()"),
            "TicTacToe game Spot must mirror the .NET lifecycle, timer, and turn-timeout API usage");
        assertTrue(gameSpotSource.contains("onPostActorJoinedAsync(")
                && gameSpotSource.contains("onActorLeftAsync(")
                && !gameSpotSource.contains("ZLinkSpotActorChange" + "Result")
                && !entrySpotSource.contains("onActorJoinAsync"),
            "TicTacToe EntrySpot and GameSpot lifecycle must use member callbacks without change-result arguments");
        assertTrue(playActorJoinHandlerSource.contains("request.roomId()"),
            "TicTacToe join handler must store the requested room id");
        assertTrue(playPlaceMarkHandlerSource.contains("actor.requireJoinedGame()"),
            "TicTacToe place handler must require actor join state");
        assertTrue(playActorSource.contains("joinedRoomId"),
            "TicTacToe PlayActor must own joined room state");
        assertTrue(playActorSource.contains("joinGame"),
            "TicTacToe PlayActor must expose joinGame state transition");
        assertTrue(playActorSource.contains("requireJoinedGame"),
            "TicTacToe PlayActor must validate joined game state");
        assertTrue(entrySpotSource.contains("PlayEntrySpot(ZLinkEntrySpotContext context)")
                && !entrySpotSource.contains("public PlayEntrySpot()")
                && gameSpotSource.contains("ZLinkSpotContext context")
                && gameSpotSource.contains("TicTacToeGameCreatedHandler createdHandler")
                && !gameSpotSource.contains("public TicTacToeGame()")
                && !gameSpotSource.contains("SampleSpotContext")
                && !gameSpotSource.contains("CompletedSpotOutbound")
                && !gameSpotSource.contains("TicTacToeGameDirectory"),
            "TicTacToe Spot instances must be created by the framework runtime, not sample-owned fallback contexts");
        assertTrue(playSource.contains(".addStreamNode("),
            "TicTacToe direct sample must register the STREAM entry point");
        assertFalse(playSource.contains("attachActorGateway("),
            "TicTacToe direct sample must rely on local managed actor binding like the .NET direct sample");
        assertFalse(serverProgramSource.contains("CreateGameHandler"),
            "TicTacToe role Program must not collapse Play handler wiring into the entry point");
    }

    @Test
    void ticTacToeKotlinSampleMirrorsJavaRoleLayout() throws IOException {
        assertNoSampleSourcesUnder("kotlin", "TicTacToe", "src/main/kotlin",
            List.of(
                "systems/zlink/samples/kotlin/tictactoe/client",
                "systems/zlink/samples/kotlin/tictactoe/server",
                "systems/zlink/samples/kotlin/tictactoe/shared"));
        assertSampleFilesExist("kotlin", "TicTacToe", "Client/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientArguments.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClient.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientOptions.kt",
            "systems/zlink/samples/kotlin/tictactoe/client/TicTacToeSampleDefaults.kt"));
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Client",
                "README.md", "Tic Tac Toe Client"),
            "Kotlin TicTacToe Client role must include a standalone README");
        assertSampleFilesExist("kotlin", "TicTacToe", "Server/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServerApplication.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/AuthenticatePlayerHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/api/handlers/CreateGameHttpHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/TicTacToeServerApplicationGroup.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleLogging.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SamplePorts.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/configuration/SampleSettings.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/PlayServer.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/PlayServerApplication.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/actors/PlayActor.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/actors/PlayActorFactory.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/application/gamecreation/TicTacToeGameCreator.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/PlayEntrySpot.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/TicTacToeGame.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorPlaceMarkHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/handlers/TicTacToeGameTimerHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/handlers/CreateGameHandler.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/sessions/PlaySession.kt",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/sessions/handlers/AuthenticatePlaySessionHandler.kt"));
        assertSampleFilesExist("kotlin", "TicTacToe", "Shared/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/tictactoe/shared/contracts/Contracts.kt"));
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Client/src/main/kotlin",
                "systems/zlink/samples/kotlin/tictactoe/client/Program.kt", "TicTacToeClientArguments.parse"),
            "Kotlin TicTacToe Client role Program must live in the Client project folder");
        assertTrue(sampleFileContains("kotlin", "TicTacToe", "Server/src/main/kotlin",
                "systems/zlink/samples/kotlin/tictactoe/server/Program.kt", "PlayServerApplication.run(settings)"),
            "Kotlin TicTacToe Server role Program must live in the Server project folder");

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
        String clientProgramSource = sampleKotlinSource(
            "TicTacToe",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/client/Program.kt");
        String apiSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServer.kt");
        String apiHostFactorySource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/api/ApiServerApplication.kt");
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
        String playHostFactorySource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/PlayServerApplication.kt");
        String playActorSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/actors/PlayActor.kt");
        String entrySpotSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/PlayEntrySpot.kt");
        String gameSpotSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/TicTacToeGame.kt");
        String playSessionSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/sessions/PlaySession.kt");
        String playAuthHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/sessions/handlers/AuthenticatePlaySessionHandler.kt");

        assertTrue(serverProgramSource.contains("\"api\" -> ApiServerApplication.run(settings)")
                && serverProgramSource.contains("\"play\" -> PlayServerApplication.run(settings)")
                && serverProgramSource.contains("\"all\" -> runAll(settings)")
                && serverProgramSource.contains("\"client\" -> runClient(settings)")
                && serverProgramSource.contains("SampleSettings.fromArgs(args)")
                && serverProgramSource.contains("TicTacToeServerApplicationGroup.run(effectiveSettings)")
                && !serverProgramSource.contains("\"server\"")
                && serverBuildSource.contains("implementation(sampleProject(\"Client\"))"),
            "Kotlin TicTacToe Server Program must expose .NET-style all/play/api/client modes");
        assertFalse(serverProgramSource.contains("CountDownLatch")
                || serverProgramSource.contains("ZLinkFramework.start"),
            "Kotlin TicTacToe Server roles must rely on Spring lifecycle keep-alive instead of direct framework execution");
        assertTrue(apiSource.contains("ZLinkFrameworkConfigurer")
                && apiSource.contains("options.codecs().addMessagePack()")
                && playSource.contains("ZLinkFrameworkConfigurer")
                && playSource.contains("options.codecs().addMessagePack()")
                && apiHostFactorySource.contains("@SpringBootApplication")
                && apiHostFactorySource.contains("SpringApplicationBuilder")
                && apiHostFactorySource.contains(".web(WebApplicationType.SERVLET)")
                && apiHostFactorySource.contains("setKeepAlive(true)")
                && apiHostFactorySource.contains("ApiServer.configure(settings)")
                && playHostFactorySource.contains("@SpringBootApplication")
                && playHostFactorySource.contains("SpringApplicationBuilder")
                && playHostFactorySource.contains(".web(WebApplicationType.NONE)")
                && playHostFactorySource.contains("setKeepAlive(true)")
                && playHostFactorySource.contains("PlayServer.configure(settings)"),
            "Kotlin TicTacToe direct Api and Play framework hosts must enable MessagePack codecs and expose HTTP create-game with shared settings");
        assertTrue(settingsSource.contains("withEphemeralDefaults()")
                && settingsSource.contains("SamplePorts.reserve()")
                && settingsSource.contains("--api-bind")
                && settingsSource.contains("--api-url")
                && settingsSource.contains("--api-channel-endpoint")
                && settingsSource.contains("--play-channel-endpoint")
                && settingsSource.contains("--play-router-endpoint")
                && settingsSource.contains("--play-endpoint")
                && !settingsSource.contains("currentValue")
                && !settingsSource.contains("fun current")
                && !settingsSource.contains("setCurrent"),
            "Kotlin TicTacToe direct sample must expose .NET-style sample settings through Spring DI instead of fixed topology constants or global state");
        assertTrue(clientSource.contains("HttpClient")
                && clientSource.contains("CreateGameHttpReq")
                && clientSource.contains("CreateGameHttpRes")
                && clientSource.contains(".resolve(\"/games\")")
                && !clientSource.contains(".requestToChannel("),
            "Kotlin TicTacToe direct client must create games through the HTTP API path");
        assertTrue(playAuthHandlerSource.contains("AuthenticatePlayerReq(request.accessToken)")
                && playAuthHandlerSource.contains(".submit(AuthenticatePlayerRes::class.java)")
                && authHandlerSource.contains("suspend fun handle(request: AuthenticatePlayerReq): AuthenticatePlayerRes"),
            "Kotlin TicTacToe direct Play session AuthenticatePlayer path must use typed request and response contracts");
        assertTrue(createGameHandlerSource.contains("@RestController")
                && createGameHandlerSource.contains("@PostMapping(\"/games\")")
                && createGameHandlerSource.contains("suspend fun handle(@RequestBody request: CreateGameHttpReq): CreateGameHttpRes")
                && createGameHandlerSource.contains("CreateGameHttpReq")
                && createGameHandlerSource.contains("CreateGameHttpRes")
                && createGameHandlerSource.contains("CreateGameReq")
                && createGameHandlerSource.contains(".requestToChannel(")
                && createGameHandlerSource.contains(".timeout(SampleNames.RequestTimeout)")
                && createGameHandlerSource.contains(".submit(CreateGameRes::class.java)")
                && !createGameHandlerSource.contains(".packetName(\"CreateGameReq\")")
                && !createGameHandlerSource.contains("HttpExchange")
                && !createGameHandlerSource.contains("HttpServer"),
            "Kotlin TicTacToe HTTP create-game endpoint must run as a Spring controller and translate to the typed Play channel request");
        assertTrue(clientSource.contains("ZLinkStreamConnectorFactory.create"),
            "Kotlin TicTacToe Client role must use the public stream connector for play requests");
        assertTrue(clientSource.contains("ZLinkKotlinStreamConnector")
                && clientSource.contains(".kotlin()")
                && clientSource.contains("hostStream.close().await()")
                && clientSource.contains("guestStream.close().await()")
                && !clientSource.contains(".submit()"),
            "Kotlin TicTacToe client scenario must use coroutine connector wrappers without direct submit calls");
        assertTrue(clientSource.contains(".request(AuthenticateReq(options.xActorId)).await<AuthenticateRes>()")
                && clientSource.contains("AuthenticateReq(options.xActorId)")
                && clientSource.contains("JoinGameReq(game.roomId)")
                && clientSource.contains("PlaceMarkReq(3)")
                && clientSource.contains("PlaceMarkReq(4)")
                && clientSource.contains("PlaceMarkReq(2)")
                && clientSource.contains("URI.create(endpoint)")
                && clientSource.contains("ensure(hostWin.state.status == \"Won\")")
                && clientSource.contains("ensure(hostWin.state.winner == options.xActorId)")
                && clientSource.contains("ZLinkStreamMessagePack.codec()")
                && !clientSource.contains("ZLinkStreamMessagePack.request")
                && !clientSource.contains(FORBIDDEN_TICTACTOE_RESULT)
                && !clientSource.contains("game.gameId"),
            "Kotlin TicTacToe stream client path must use connector member request contracts and assert the .NET winning scenario");
        assertTrue(clientSource.contains("hostStream.waitFor<PlayerJoinedNotify>()")
                && clientSource.contains("hostStream.waitFor<GameStateNotify>()")
                && clientSource.contains("guestStream.waitFor<GameStateNotify>()")
                && clientSource.contains("ZLinkStreamDispatchMode.AUTO")
                && clientSource.contains("hostSawGuestJoin")
                && clientSource.contains("hostSawGameStart")
                && clientSource.contains("guestSawHostWin")
                && clientSource.contains(".where { message -> message.payload().state.lastMoveCell == 0 }")
                && clientSource.contains(".where { message -> message.payload().state.status == \"Won\" }")
                && !clientSource.contains("ZLinkStreamMessagePack.on")
                && !clientSource.contains("ZLinkStreamMessagePack.waitForAsync")
                && !clientSource.contains("ConcurrentLinkedQueue")
                && !clientSource.contains("stateNotifications")
                && !clientSource.contains("playerJoinedNotifications")
                && clientProgramSource.contains("TicTacToeClient().run(clientOptions)")
                && !clientProgramSource.contains("writeTo(System.out)"),
            "Kotlin TicTacToe direct client must subscribe to typed stream notifications without returning a result DTO");
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
                && authHandlerSource.contains("@ZLinkRequest(packetName = \"AuthenticatePlayerReq\")")
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
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/handlers/CreateGameHandler.kt");
        assertTrue(playCreateGameHandlerSource.contains("suspend fun create(request: CreateGameReq): CreateGameRes")
                && playCreateGameHandlerSource.contains("ZLinkSpotManager")
                && playCreateGameHandlerSource.contains("settings: SampleSettings")
                && playCreateGameHandlerSource.contains("@ZLinkHandlerGroup(SampleNames.PlayChannel)")
                && playCreateGameHandlerSource.contains("request: CreateGameReq")
                && playCreateGameHandlerSource.contains("gameCreator.nextRoom(request.gameName)")
                && playCreateGameHandlerSource.contains("RoutingId.from(room.roomId)")
                && playCreateGameHandlerSource.contains("CreateGameRes(")
                && !playCreateGameHandlerSource.contains("SampleSettings.current()"),
            "Kotlin TicTacToe Play CreateGame handler must be created by Spring DI, create a Spot, and reply with typed contracts");
        assertTrue(clientSource.contains("JoinGameReq(game.roomId)")
                && !clientSource.contains("TicTacToeGameJoinReq"),
            "Kotlin TicTacToe client must use client-facing JoinGame contracts only");
        String playActorJoinHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorJoinGameHandler.kt");
        String playPlaceMarkHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorPlaceMarkHandler.kt");
        String gameCreatedHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/handlers/TicTacToeGameCreatedHandler.kt");
        String gameTimerHandlerSource = sampleKotlinSource(
            "TicTacToe",
            "Server/src/main/kotlin",
            "systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/handlers/TicTacToeGameTimerHandler.kt");
        assertTrue(playAuthHandlerSource.contains("ZLinkStreamMessagePack.decode")
                && playAuthHandlerSource.contains("AuthenticatePlayerReq(request.accessToken)")
                && playAuthHandlerSource.contains("context.actors().bindAsync(playActor).await()")
                && playSessionSource.contains("handlers.tryHandleAsync(context, header, payload)")
                && playSessionSource.contains("requireActor(header.packetName()).relayAsync(header, payload)")
                && !playSessionSource.contains("joinEntrySpot(")
                && !playSessionSource.contains("joinSpot(RoutingId.fromHex")
                && !playSessionSource.contains("split(\"|\")"),
            "Kotlin TicTacToe Play stream session must authenticate through the Api role and relay actor packets");
        assertTrue(gameSpotSource.contains("override suspend fun onActorJoin(")
                && gameSpotSource.contains("request: Message")
                && gameSpotSource.contains("ZLinkSpotActorJoinResponse.accept")
                && gameSpotSource.contains("TicTacToeGameJoinReq::class.java")
                && gameSpotSource.contains("TicTacToeGameJoinRes")
                && playActorJoinHandlerSource.contains("TicTacToeGameJoinReq")
                && playActorJoinHandlerSource.contains("entrySpot: PlayEntrySpot")
                && playActorJoinHandlerSource.contains("context: ZLinkSpotActorRequestContext")
                && playActorJoinHandlerSource.contains("cancellationToken: CancellationToken")
                && playActorJoinHandlerSource.contains("JoinGameRes(result.reply().state)")
                && playPlaceMarkHandlerSource.contains("spot: TicTacToeGame")
                && playPlaceMarkHandlerSource.contains("context: ZLinkSpotActorRequestContext")
                && playPlaceMarkHandlerSource.contains("request: PlaceMarkReq")
                && playPlaceMarkHandlerSource.contains("cancellationToken: CancellationToken"),
            "Kotlin TicTacToe Play actor join admission must be a Message-based Spot member callback while game packets stay typed");
        assertTrue(gameSpotSource.contains("actor.joinGame")
                && gameSpotSource.contains("boundSession()")
                && gameSpotSource.contains(".send(GameStateNotify")
                && gameSpotSource.contains(".send(message)"),
            "Kotlin TicTacToe game Spot must own joined-game state transitions and typed bound-session notifications");
        assertTrue(gameSpotSource.contains("override suspend fun onInitialize()")
                && gameSpotSource.contains("context.addTimer(")
                && gameSpotSource.contains("TicTacToeGameTimerHandler::class.java")
                && gameSpotSource.contains("override suspend fun onClosing()")
                && gameSpotSource.contains("gameTick?.cancelAsync()")
                && gameSpotSource.contains("TurnTimedOut")
                && gameSpotSource.contains("resetTurnDeadline()")
                && gameSpotSource.contains("suspend fun tick()")
                && gameSpotSource.contains("override suspend fun onCreate(request: Message)")
                && gameSpotSource.contains("fun markCreated(request: Message)")
                && gameSpotSource.contains("ensureCreated()")
                && gameCreatedHandlerSource.contains("fun handle(")
                && gameCreatedHandlerSource.contains("request: Message")
                && gameCreatedHandlerSource.contains("game.markCreated(request)")
                && gameTimerHandlerSource.contains("ZLinkCoroutineSpotTimerHandler<TicTacToeGame>")
                && gameTimerHandlerSource.contains("spot.tick()"),
            "Kotlin TicTacToe game Spot must mirror the .NET lifecycle, timer, and turn-timeout API usage");
        assertTrue(gameSpotSource.contains("override suspend fun onPostActorJoined(")
                && gameSpotSource.contains("override suspend fun onActorLeft(")
                && !gameSpotSource.contains("ZLinkSpotActorChange" + "Result")
                && !entrySpotSource.contains("onActorJoinAsync"),
            "Kotlin TicTacToe EntrySpot and GameSpot lifecycle must use member callbacks without change-result arguments");
        assertTrue(playActorJoinHandlerSource.contains("request.roomId"),
            "Kotlin TicTacToe join handler must store the requested room id");
        assertTrue(playPlaceMarkHandlerSource.contains("actor.requireJoinedGame()"),
            "Kotlin TicTacToe place handler must require actor join state");
        assertTrue(playActorSource.contains("joinedRoomId"),
            "Kotlin TicTacToe PlayActor must own joined room state");
        assertTrue(playActorSource.contains("fun joinGame"),
            "Kotlin TicTacToe PlayActor must expose joinGame state transition");
        assertTrue(playActorSource.contains("fun requireJoinedGame"),
            "Kotlin TicTacToe PlayActor must validate joined game state");
        assertTrue(entrySpotSource.contains("context: ZLinkEntrySpotContext")
                && !entrySpotSource.contains("ZLinkEntrySpotContext? = null")
                && gameSpotSource.contains("private val context: ZLinkSpotContext")
                && gameSpotSource.contains("private val createdHandler: TicTacToeGameCreatedHandler")
                && !gameSpotSource.contains("constructor()")
                && !gameSpotSource.contains("SampleSpotContext")
                && !gameSpotSource.contains("CompletedSpotOutbound")
                && !gameSpotSource.contains("TicTacToeGameDirectory"),
            "Kotlin TicTacToe Spot instances must be created by the framework runtime, not sample-owned fallback contexts");
        assertTrue(playSource.contains(".addStreamNode("),
            "Kotlin TicTacToe direct sample must register the STREAM entry point");
        assertFalse(playSource.contains("attachActorGateway("),
            "Kotlin TicTacToe direct sample must rely on local managed actor binding like the .NET direct sample");
        assertFalse(serverProgramSource.contains("CreateGameHandler"),
            "Kotlin TicTacToe role Program must not collapse Play handler wiring into the entry point");
    }

    @Test
    void bingoMirrorsFourClientMatchingTimerAndBoundPushGate() throws IOException {
        assertNoSampleSourcesUnder("java", "Bingo", "src/main/java");
        assertSampleFilesExist("java", "Bingo", "Client/src/main/java", List.of(
            "systems/zlink/samples/bingo/client/Program.java",
            "systems/zlink/samples/bingo/client/BingoClientApp.java"));
        assertSampleFilesExist("java", "Bingo", "Probe/src/main/java", List.of(
            "systems/zlink/samples/bingo/probe/Program.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Api/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/api/Program.java",
            "systems/zlink/samples/bingo/server/api/ApiServerApplication.java",
            "systems/zlink/samples/bingo/server/api/handlers/AuthenticatePlayerHandler.java",
            "systems/zlink/samples/bingo/server/api/handlers/MatchBingoHandler.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Play/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/play/Program.java",
            "systems/zlink/samples/bingo/server/play/PlayServerApplication.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/actors/PlayerActor.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/actors/PlayerActorFactory.java",
            "systems/zlink/samples/bingo/server/play/domain/bingo/BingoCard.java",
            "systems/zlink/samples/bingo/server/play/domain/bingo/BingoGame.java",
            "systems/zlink/samples/bingo/server/play/domain/bingo/BingoRoomGame.java",
            "systems/zlink/samples/bingo/server/play/domain/bingo/BingoRoomModels.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/notifications/BingoNotificationPublisher.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/spots/BingoRoomSpot.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/spots/handlers/BingoRoomSpotCreatedHandler.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/spots/handlers/BingoRoomTimerHandler.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/spots/handlers/SubmitBingoCardHandler.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/spots/BingoEntrySpot.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/spots/handlers/MatchBingoActorHandler.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/handlers/AllocateBingoRoomHandler.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/handlers/BingoRoomDirectory.java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/handlers/EnsurePlayerActorHandler.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Registry/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/registry/Program.java",
            "systems/zlink/samples/bingo/server/registry/RegistryApplication.java"));
        assertSampleFilesExist("java", "Bingo", "Server/Session/src/main/java", List.of(
            "systems/zlink/samples/bingo/server/session/Program.java",
            "systems/zlink/samples/bingo/server/session/SessionServerApplication.java",
            "systems/zlink/samples/bingo/server/session/sessions/BingoSession.java",
            "systems/zlink/samples/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.java"));
        assertSampleFilesExist("java", "Bingo", "Shared/src/main/java", List.of(
            "systems/zlink/samples/bingo/shared/configuration/SampleNames.java",
            "systems/zlink/samples/bingo/shared/configuration/SampleTopology.java",
            "systems/zlink/samples/bingo/shared/contracts/Messages.java"));

        String rootBuildSource = sampleFile(
            "java",
            "Bingo",
            "",
            "build.gradle.kts");
        String clientProgramSource = sampleJavaSource(
            "Bingo",
            "Client/src/main/java",
            "systems/zlink/samples/bingo/client/Program.java");
        String clientAppSource = sampleJavaSource(
            "Bingo",
            "Client/src/main/java",
            "systems/zlink/samples/bingo/client/BingoClientApp.java");
        String roomSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/spots/BingoRoomSpot.java");
        String publisherSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/adapters/zlink/notifications/BingoNotificationPublisher.java");
        String apiHostSource = sampleJavaSource(
            "Bingo",
            "Server/Api/src/main/java",
            "systems/zlink/samples/bingo/server/api/ApiServerApplication.java");
        String playHostSource = sampleJavaSource(
            "Bingo",
            "Server/Play/src/main/java",
            "systems/zlink/samples/bingo/server/play/PlayServerApplication.java");
        String registryHostSource = sampleJavaSource(
            "Bingo",
            "Server/Registry/src/main/java",
            "systems/zlink/samples/bingo/server/registry/RegistryApplication.java");
        String sessionHostSource = sampleJavaSource(
            "Bingo",
            "Server/Session/src/main/java",
            "systems/zlink/samples/bingo/server/session/SessionServerApplication.java");
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
            "systems/zlink/samples/bingo/server/play/adapters/zlink/handlers/EnsurePlayerActorHandler.java");

        assertTrue(rootBuildSource.contains("plugins {\n    base\n}")
                && !rootBuildSource.contains("application"),
            "Bingo root project must not expose an aggregate in-process runner");
        assertTrue(clientProgramSource.contains("ZLinkStreamConnector client1 = createClient()")
                && clientProgramSource.contains("ZLinkStreamConnector client2 = createClient()")
                && clientProgramSource.contains("new BingoClientApp().run(client1, client2)"),
            "Bingo client Program must create two configured connectors and pass them into the scenario app");
        assertTrue(clientProgramSource.contains("client1.close().await()")
                && clientProgramSource.contains("client2.close().await()"),
            "Bingo client Program must close connectors through lifecycle call builders");
        assertTrue(clientProgramSource.contains("ZLinkStreamConnectorFactory.create"),
            "Bingo sample must use connector public factory");
        assertTrue(clientProgramSource.contains("ZLinkStreamDispatchMode.AUTO"),
            "Bingo sample must use configured auto-dispatch connectors like the .NET immediate-dispatch sample");
        assertTrue(clientAppSource.contains("ZLinkStreamConnector client1")
                && clientAppSource.contains("ZLinkStreamConnector client2")
                && clientAppSource.contains("SubmitBingoCardReq")
                && clientAppSource.contains("MatchBingoReq(\"two-player\")")
                && clientAppSource.contains("client1.waitFor(SampleNames.PlayerJoinedPacket)")
                && clientAppSource.contains("request(new Messages.AuthenticateReq")
                && clientAppSource.contains(".await(Messages.AuthenticateRes.class)")
                && clientAppSource.contains(".submit(Messages.PlayerJoinedNotify.class)")
                && clientAppSource.contains("client1.await(client1SawClient2Join)")
                && !clientAppSource.contains("ZLinkStreamProtobuf.")
                && clientAppSource.contains("List.of(1, 2, 3, 4, 0, 6, 7, 8, 9)")
                && clientAppSource.contains("client1Result.winners().equals(List.of(client1Auth.actorId()))")
                && roomSource.contains("submitCardAsync")
                && roomSource.contains("game.drawNext()"),
            "Bingo sample must use connector member APIs, submit .NET baseline cards, and let the server timer draw numbers");
        assertTrue(clientProgramSource.contains("ZLinkStreamProtobuf.codec()"),
            "Bingo client Program must configure the codec outside the scenario app");
        assertTrue(!clientAppSource.contains("server.play")
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
                && apiHostSource.contains("ZLinkFrameworkConfigurer")
                && !apiHostSource.contains("ZLinkFramework.start")
                && playHostSource.contains("@SpringBootApplication")
                && playHostSource.contains("SpringApplicationBuilder")
                && playHostSource.contains("ZLinkFrameworkConfigurer")
                && !playHostSource.contains("ZLinkFramework.start")
                && sessionHostSource.contains("@SpringBootApplication")
                && sessionHostSource.contains("SpringApplicationBuilder")
                && sessionHostSource.contains("ZLinkFrameworkConfigurer")
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
                && apiHandlerSource.contains("implements ZLinkRequestHandler<")
                && apiHandlerSource.contains("Messages.AuthenticatePlayerReq")
                && playHandlerSource.contains("@ZLinkHandlerGroup(\"play\")")
                && playHandlerSource.contains("implements ZLinkRequestHandler<")
                && playHandlerSource.contains("Messages.EnsurePlayerActorReq"),
            "Bingo Api/Play handlers must use interface-based request mapping");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Api/src/main/java",
                "systems/zlink/samples/bingo/server/api/Program.java", "ApiServerApplication.run"),
            "Java Bingo Api role must have its own executable Program");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Play/src/main/java",
                "systems/zlink/samples/bingo/server/play/Program.java", "PlayServerApplication.run"),
            "Java Bingo Play role must have its own executable Program");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Session/src/main/java",
                "systems/zlink/samples/bingo/server/session/Program.java", "SessionServerApplication.run"),
            "Java Bingo Session role must have its own executable Program");
        assertTrue(sampleFileContains("java", "Bingo", "Server/Registry/src/main/java",
                "systems/zlink/samples/bingo/server/registry/Program.java", "RegistryApplication.run"),
            "Java Bingo Registry role must have its own executable Program");
        assertFalse(publisherSource.contains("BingoPlayerClient"),
            "Bingo server push must go through framework bound sessions, not direct client objects");
    }

    @Test
    void bingoKotlinSampleMirrorsJavaRoleLayout() throws IOException {
        assertNoSampleSourcesUnder("kotlin", "Bingo", "src/main/kotlin");
        assertSampleFilesExist("kotlin", "Bingo", "Client/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/client/Program.kt",
            "systems/zlink/samples/kotlin/bingo/client/BingoClientApp.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Probe/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/probe/Program.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Api/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/api/Program.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/ApiServerApplication.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/AuthenticatePlayerHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/api/handlers/MatchBingoHandler.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Play/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/play/Program.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/PlayServerApplication.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/actors/PlayerActor.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/actors/PlayerActorFactory.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/domain/bingo/BingoCard.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/notifications/BingoNotificationPublisher.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/domain/bingo/BingoRoomModels.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/BingoRoomSpot.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/handlers/BingoRoomSpotCreatedHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/handlers/BingoRoomTimerHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/handlers/SubmitBingoCardHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/BingoEntrySpot.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/handlers/MatchBingoActorHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/handlers/AllocateBingoRoomHandler.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/handlers/BingoRoomDirectory.kt",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/handlers/EnsurePlayerActorHandler.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Registry/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/registry/Program.kt",
            "systems/zlink/samples/kotlin/bingo/server/registry/RegistryApplication.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Server/Session/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/server/session/Program.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/SessionServerApplication.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/sessions/BingoSession.kt",
            "systems/zlink/samples/kotlin/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.kt"));
        assertSampleFilesExist("kotlin", "Bingo", "Shared/src/main/kotlin", List.of(
            "systems/zlink/samples/kotlin/bingo/shared/configuration/SampleNames.kt",
            "systems/zlink/samples/kotlin/bingo/shared/configuration/SampleTopology.kt",
            "systems/zlink/samples/kotlin/bingo/shared/contracts/Messages.kt"));

        String rootBuildSource = sampleFile(
            "kotlin",
            "Bingo",
            "",
            "build.gradle.kts");
        String clientProgramSource = sampleKotlinSource(
            "Bingo",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/client/Program.kt");
        String clientAppSource = sampleKotlinSource(
            "Bingo",
            "Client/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/client/BingoClientApp.kt");
        String roomSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/BingoRoomSpot.kt");
        String publisherSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/notifications/BingoNotificationPublisher.kt");
        String apiHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Api/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/api/ApiServerApplication.kt");
        String playHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Play/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/play/PlayServerApplication.kt");
        String registryHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Registry/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/registry/RegistryApplication.kt");
        String sessionHostSource = sampleKotlinSource(
            "Bingo",
            "Server/Session/src/main/kotlin",
            "systems/zlink/samples/kotlin/bingo/server/session/SessionServerApplication.kt");
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
            "systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/handlers/EnsurePlayerActorHandler.kt");

        assertTrue(rootBuildSource.contains("plugins {\n    base\n}")
                && !rootBuildSource.contains("application"),
            "Kotlin Bingo root project must not expose an aggregate in-process runner");
        assertTrue(clientProgramSource.contains("val client1 = createClient()")
                && clientProgramSource.contains("val client2 = createClient()")
                && clientProgramSource.contains("BingoClientApp().run(client1, client2)"),
            "Kotlin Bingo client Program must create two configured connectors and pass them into the scenario app");
        assertTrue(clientProgramSource.contains(".kotlin()")
                && clientProgramSource.contains("client1.close().await()")
                && clientProgramSource.contains("client2.close().await()"),
            "Kotlin Bingo client Program must close connectors through coroutine lifecycle wrappers");
        assertTrue(clientProgramSource.contains("ZLinkStreamConnectorFactory.create"),
            "Kotlin Bingo sample must use connector public factory");
        assertTrue(clientProgramSource.contains("ZLinkStreamDispatchMode.AUTO"),
            "Kotlin Bingo sample must use configured auto-dispatch connectors like the .NET immediate-dispatch sample");
        assertTrue(clientAppSource.contains("client1: ZLinkKotlinStreamConnector")
                && clientAppSource.contains("client2: ZLinkKotlinStreamConnector")
                && clientAppSource.contains("SubmitBingoCardReq")
                && clientAppSource.contains("MatchBingoReq(\"two-player\")")
                && clientAppSource.contains("client1.waitFor<PlayerJoinedNotify>()")
                && clientAppSource.contains(".where { message -> message.payload().drawSeq == 1 }")
                && clientAppSource.contains(".request(AuthenticateReq")
                && clientAppSource.contains(".await<AuthenticateRes>()")
                && !clientAppSource.contains(".submit()")
                && !clientAppSource.contains("ZLinkStreamProtobuf.")
                && clientAppSource.contains("listOf(1, 2, 3, 4, 0, 6, 7, 8, 9)")
                && clientAppSource.contains("client1Result.winners == listOf(client1Auth.actorId)")
                && roomSource.contains("submitCard")
                && roomSource.contains("allCardsSubmitted()"),
            "Kotlin Bingo sample must use connector member APIs, submit .NET baseline cards, and let the server timer draw numbers");
        assertTrue(clientProgramSource.contains("ZLinkStreamProtobuf.codec()"),
            "Kotlin Bingo client Program must configure the codec outside the scenario app");
        assertTrue(!clientAppSource.contains("server.play")
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
                && apiHostSource.contains("ZLinkFrameworkConfigurer")
                && !apiHostSource.contains("ZLinkFramework.start")
                && playHostSource.contains("@SpringBootApplication")
                && playHostSource.contains("SpringApplicationBuilder")
                && playHostSource.contains("ZLinkFrameworkConfigurer")
                && !playHostSource.contains("ZLinkFramework.start")
                && sessionHostSource.contains("@SpringBootApplication")
                && sessionHostSource.contains("SpringApplicationBuilder")
                && sessionHostSource.contains("ZLinkFrameworkConfigurer")
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
                && apiHandlerSource.contains(": ZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes>")
                && playHandlerSource.contains("@ZLinkHandlerGroup(\"play\")")
                && playHandlerSource.contains(": ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes>"),
            "Kotlin Bingo Api/Play handlers must use interface-based request mapping");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Api/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/api/Program.kt", "ApiServerApplication.run"),
            "Kotlin Bingo Api role must have its own executable Program");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Play/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/play/Program.kt", "PlayServerApplication.run"),
            "Kotlin Bingo Play role must have its own executable Program");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Session/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/session/Program.kt", "SessionServerApplication.run"),
            "Kotlin Bingo Session role must have its own executable Program");
        assertTrue(sampleFileContains("kotlin", "Bingo", "Server/Registry/src/main/kotlin",
                "systems/zlink/samples/kotlin/bingo/server/registry/Program.kt", "RegistryApplication.run"),
            "Kotlin Bingo Registry role must have its own executable Program");
        assertFalse(publisherSource.contains("BingoPlayerClient"),
            "Kotlin Bingo server push must go through framework bound sessions, not direct client objects");
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

    private static void assertNoSampleSourcesUnder(
        String language,
        String sample,
        String sourceRoot) throws IOException {
        Path sampleSourceRoot = samplesRoot()
            .resolve(language)
            .resolve(sample)
            .resolve(sourceRoot);
        if (!Files.exists(sampleSourceRoot)) {
            return;
        }
        try (var paths = Files.walk(sampleSourceRoot)) {
            assertTrue(paths.noneMatch(SampleReleaseGateContractTest::isSampleSource),
                "aggregate source root must not contain sample sources: "
                    + language + "/" + sample + "/" + sourceRoot);
        }
    }

    private static boolean isSampleSource(Path path) {
        String pathText = path.toString();
        return pathText.endsWith(".java") || pathText.endsWith(".kt");
    }

    private static boolean isServerRoleSource(Path path) {
        String normalized = path.toString().replace('\\', '/');
        return normalized.contains("/Server/Api/")
            || normalized.contains("/Server/Play/")
            || normalized.contains("/Server/Registry/")
            || normalized.contains("/Server/Session/");
    }

    private static List<String> forbiddenDirectServerStarts(Path path) {
        try {
            String content = Files.readString(path);
            return List.of(
                    "ZLinkFramework.start",
                    "ZLinkRegistry.start",
                    "CountDownLatch")
                .stream()
                .filter(content::contains)
                .toList();
        } catch (IOException ex) {
            throw new IllegalStateException("failed to read " + path, ex);
        }
    }

    private static boolean isNotSpringBootHostFactory(Path path) {
        try {
            String content = Files.readString(path);
            return !content.contains("@SpringBootApplication")
                || !content.contains("SpringApplicationBuilder");
        } catch (IOException ex) {
            throw new IllegalStateException("failed to read " + path, ex);
        }
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
