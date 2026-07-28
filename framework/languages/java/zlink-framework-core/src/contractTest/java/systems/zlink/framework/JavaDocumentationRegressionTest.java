package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;

final class JavaDocumentationRegressionTest {
    private static final Pattern SNAPSHOT = Pattern.compile("^([0-9a-f]{64}) (\\S+\\.ko\\.md)$");
    private static final Pattern SCENARIO = Pattern.compile("\\b[A-Z]{2,3}-[A-Z][0-9]+\\b");

    @Test
    void canonicalCommonSpecOwnsLiveJavaContracts() {
        Path root = repositoryRoot();
        Path commonSpec = root.resolve("framework/doc/framework/common/spec");
        Path deletedSpec = root.resolve("framework/doc/framework/spec");

        assertFalse(Files.exists(deletedSpec));
        assertTrue(Files.isRegularFile(commonSpec.resolve("13-mesh-node.ko.md")));
        assertTrue(Files.isRegularFile(
            commonSpec.resolve("server/languages/java/02-handler-interfaces.ko.md")));
        assertTrue(Files.isDirectory(
            commonSpec.resolve("server/languages/java/interfaces")));
    }

    @Test
    void everyCommonScenarioIdHasAnActiveJavaFixtureAndAllRunnerSuite() throws Exception {
        Path root = repositoryRoot();
        Path commonE2e = root.resolve("framework/doc/framework/common/e2e");
        Path javaE2e = root.resolve("framework/languages/java/e2e");

        // Config 8은 세 terminator(submit/async/yield) 계약으로 다시 쓴 목표 문서다. 현재 Java
        // 구현은 자동 turn dispatch에 머물러 있어 TD-* fixture가 없다. 그 차이는 공통 spec의 구현
        // 차이 문서가 소유한다 — 여기서는 갭이 기록돼 있는지를 검증하고 TD-*는 분모에서 뺀다.
        Set<String> expected = new HashSet<>();
        try (Stream<Path> files = Files.list(commonE2e)) {
            for (Path file : files.filter(path -> path.getFileName().toString().matches("config-[0-9]+-.+\\.ko\\.md"))
                .toList()) {
                Matcher matcher = SCENARIO.matcher(Files.readString(file));
                while (matcher.find()) expected.add(matcher.group());
            }
        }
        Set<String> executionTurn = expected.stream()
            .filter(id -> id.startsWith("TD-"))
            .collect(Collectors.toCollection(java.util.TreeSet::new));
        Set<String> instanceSpot = expected.stream()
            .filter(id -> id.startsWith("IS-"))
            .collect(Collectors.toCollection(java.util.TreeSet::new));
        assertFalse(executionTurn.isEmpty(), "config-8 execution turn scenarios missing");
        assertFalse(instanceSpot.isEmpty(), "config-14 instance spot scenarios missing");
        expected.removeAll(executionTurn);
        expected.removeAll(instanceSpot);

        String executionTurnFeatureMap = Files.readString(
            javaE2e.resolve("AutomaticTurnDispatch/feature-map.ko.md"));
        for (String scenario : executionTurn) {
            assertTrue(executionTurnFeatureMap.contains(scenario),
                "missing deferred Java execution-turn scenario: " + scenario);
        }
        String instanceSpotFeatureMap = Files.readString(
            javaE2e.resolve("InstanceSpot/feature-map.ko.md"));
        for (String scenario : instanceSpot) {
            assertTrue(instanceSpotFeatureMap.contains(scenario),
                "missing deferred Java instance-spot scenario: " + scenario);
        }

        String active;
        try (Stream<Path> files = Files.walk(javaE2e)) {
            active = files.filter(Files::isRegularFile)
                .filter(path -> {
                    String name = path.getFileName().toString();
                    return name.endsWith(".java") || name.endsWith(".sh") || name.endsWith(".ko.md");
                })
                .map(JavaDocumentationRegressionTest::readUnchecked)
                .collect(Collectors.joining("\n"));
        }
        Set<String> missing = expected.stream()
            .filter(id -> !active.contains(id))
            .collect(Collectors.toCollection(java.util.TreeSet::new));
        assertTrue(missing.isEmpty(), "missing Java E2E scenario IDs: " + missing);

        String allRunner = Files.readString(javaE2e.resolve("run_e2e_all.sh"));
        assertTrue(allRunner.contains("AutomaticTurnDispatch"));
        assertTrue(allRunner.contains("StoreFailure"));
        assertTrue(allRunner.contains("ObservabilityOps"));
    }

    private static Map<String, String> parseSnapshot(String ledger) {
        Map<String, String> result = new HashMap<>();
        for (String line : ledger.lines().toList()) {
            Matcher matcher = SNAPSHOT.matcher(line);
            if (matcher.matches()) result.put(matcher.group(2), matcher.group(1));
        }
        return Map.copyOf(result);
    }

    private static Path repositoryRoot() {
        Path current = Path.of("").toAbsolutePath();
        while (current != null) {
            if (Files.isRegularFile(current.resolve("framework/languages/java/settings.gradle.kts"))
                && Files.isDirectory(current.resolve("framework/doc/framework/common/spec"))) {
                return current;
            }
            current = current.getParent();
        }
        throw new IllegalStateException("repository root not found");
    }

    private static String readUnchecked(Path path) {
        try {
            return Files.readString(path, StandardCharsets.UTF_8);
        } catch (IOException error) {
            throw new java.io.UncheckedIOException(error);
        }
    }
}
