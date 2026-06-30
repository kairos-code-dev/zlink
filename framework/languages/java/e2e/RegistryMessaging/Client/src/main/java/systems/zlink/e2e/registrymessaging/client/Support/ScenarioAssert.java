package systems.zlink.e2e.registrymessaging.client.Support;

import java.util.Arrays;
import java.util.concurrent.CompletableFuture;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class ScenarioAssert {
    private ScenarioAssert() {
    }

    public static void that(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    public static void expectFailure(Runnable action) {
        try {
            action.run();
        } catch (RuntimeException error) {
            return;
        }
        throw new IllegalStateException("operation unexpectedly succeeded");
    }

    public static String[] evidence(ZLinkHttpClient role) {
        return role.get("/evidence").fetch(String[].class);
    }

    public static String[] waitEvidence(ZLinkHttpClient role, String contains) {
        return role.post("/evidence/wait")
            .body(new Contracts.EvidenceWaitRequest(contains))
            .fetch(String[].class);
    }

    public static String[] waitAnyEvidence(
        ZLinkHttpClient first,
        ZLinkHttpClient second,
        String contains) {
        CompletableFuture<String[]> firstWait = CompletableFuture.supplyAsync(() -> waitEvidence(first, contains));
        CompletableFuture<String[]> secondWait = CompletableFuture.supplyAsync(() -> waitEvidence(second, contains));
        CompletableFuture.anyOf(firstWait, secondWait).join();
        return concat(evidence(first), evidence(second));
    }

    public static String[] concat(String[] first, String[] second) {
        return java.util.stream.Stream.concat(Arrays.stream(first), Arrays.stream(second))
            .toArray(String[]::new);
    }
}
