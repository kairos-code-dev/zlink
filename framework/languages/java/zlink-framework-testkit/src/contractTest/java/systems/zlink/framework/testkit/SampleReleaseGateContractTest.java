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
    private static final Set<String> REQUIRED_SAMPLES = Set.of(
        "TicTacToe",
        "TicTacToe.SessionGateway",
        "Bingo",
        "StreamingClient");

    private static final List<String> FORBIDDEN_SAMPLE_PATTERNS = List.of(
        "import systems.zlink.runtime.",
        "import systems.zlink.internal.",
        "RouteStore",
        "MetadataStore",
        "RemoteAddressResolver",
        "Thread.sleep",
        "sleep(",
        "session relay JSON",
        "in-memory route channel replacement");

    @Test
    void requiredSamplesExposeExecutableEntryPoints() {
        Path samplesRoot = samplesRoot();

        assertTrue(Files.isRegularFile(samplesRoot.resolve("run_samples.sh")),
            "missing aggregate sample runner");
        assertTrue(Files.isExecutable(samplesRoot.resolve("run_samples.sh")),
            "aggregate sample runner must be executable");

        for (String sample : REQUIRED_SAMPLES) {
            Path sampleRoot = samplesRoot.resolve(sample);
            assertTrue(Files.isDirectory(sampleRoot), "missing sample " + sample);
            assertTrue(Files.isRegularFile(sampleRoot.resolve("settings.gradle.kts")),
                "missing settings.gradle.kts for " + sample);
            assertTrue(Files.isRegularFile(sampleRoot.resolve("build.gradle.kts")),
                "missing build.gradle.kts for " + sample);
            assertTrue(Files.isRegularFile(sampleRoot.resolve("run_sample.sh")),
                "missing run_sample.sh for " + sample);
            assertTrue(Files.isExecutable(sampleRoot.resolve("run_sample.sh")),
                "run_sample.sh must be executable for " + sample);
        }
    }

    @Test
    void sampleSourcesUseOnlyPublicFrameworkAndConnectorApi() throws IOException {
        try (Stream<Path> files = Files.walk(samplesRoot())) {
            Map<Path, List<String>> offenders = files
                .filter(Files::isRegularFile)
                .filter(path -> path.toString().endsWith(".java"))
                .map(path -> Map.entry(path, forbiddenLines(path)))
                .filter(entry -> !entry.getValue().isEmpty())
                .collect(java.util.stream.Collectors.toMap(
                    Map.Entry::getKey,
                    Map.Entry::getValue));

            assertTrue(offenders.isEmpty(), "sample forbidden pattern offenders: " + offenders);
        }
    }

    @Test
    void ticTacToeSessionGatewayUsesActorGatewayAndFrameworkActorLocator() throws IOException {
        String source = sampleJavaSource(
            "TicTacToe.SessionGateway",
            "systems/zlink/samples/tictactoe/sessiongateway/TicTacToeSessionGatewaySample.java");

        assertTrue(source.contains("attachActorGateway(\"session-relay\")"),
            "SessionGateway sample must attach stream node to local ActorGateway SpotNode");
        assertTrue(source.contains("new ZLinkActorRef("),
            "SessionGateway sample must bind by framework actor locator");
        assertFalse(source.contains("systems.zlink.contracts.service.spot.ActorRef"),
            "SessionGateway sample must not import binding ActorRef");
        assertFalse(source.contains("new ActorRef("),
            "SessionGateway sample must not construct binding ActorRef");
        assertFalse(source.contains("ZLinkSpotRemoteAddressResolver"),
            "session handler must not call actor remote address resolver");
    }

    @Test
    void bingoMirrorsFourClientMatchingTimerAndBoundPushGate() throws IOException {
        String source = sampleJavaSource(
            "Bingo",
            "systems/zlink/samples/bingo/BingoSample.java");

        assertTrue(source.contains("i <= 4"),
            "Bingo sample must create four connector clients");
        assertTrue(source.contains("ZLinkStreamConnectorFactory.create"),
            "Bingo sample must use connector public factory");
        assertTrue(source.contains("ZLinkStreamDispatchMode.MANUAL"),
            "Bingo sample must verify manual dispatch connector path");
        assertTrue(source.contains("List.of(7, 11, 42, 42)"),
            "Bingo sample must use deterministic draw sequence");
        assertTrue(source.contains("List.of(\"player-2\", \"player-3\")"),
            "Bingo sample must verify same-sequence winners");
        assertTrue(source.contains("BingoWinner"),
            "Bingo sample must push bound client notification");
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
        assertTrue(source.contains("reconnected.connectAsync()"),
            "StreamingClient sample must include reconnect smoke");
    }

    private static Path samplesRoot() {
        return frameworkJavaRoot().resolve("samples");
    }

    private static Path frameworkJavaRoot() {
        return Path.of(System.getProperty("user.dir")).getParent();
    }

    private static String sampleJavaSource(String sample, String relativePath) throws IOException {
        return Files.readString(samplesRoot()
            .resolve(sample)
            .resolve("src/main/java")
            .resolve(relativePath));
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
