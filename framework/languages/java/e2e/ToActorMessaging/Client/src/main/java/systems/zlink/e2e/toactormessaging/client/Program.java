package systems.zlink.e2e.toactormessaging.client;

import systems.zlink.e2e.toactormessaging.shared.Contracts;
import systems.zlink.e2e.toactormessaging.shared.Env;
import systems.zlink.e2e.toactormessaging.shared.JsonHttp;
import java.util.List;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        String actorUrl = Env.get("ZLINK_JAVA_E2E_ACTOR_HTTP");
        String callerUrl = Env.get("ZLINK_JAVA_E2E_CALLER_HTTP");
        String selector = args.length == 0 ? "all" : args[0];
        require(java.util.Set.of("all", "TA-A1", "TA-A2", "TA-A3", "TA-A4",
            "TA-B1", "TA-B2", "TA-B3").contains(selector),
            "unknown ToActorMessaging selector: " + selector);

        if (selected(selector, "TA-A1")) {
            ensureReady(actorUrl, callerUrl, "TA-A1", "ta-a1");
            assertCall(callerUrl, "TA-A1-send", "ta-a1", "a1-send", "sent", true);
            assertCall(callerUrl, "TA-A1-request", "ta-a1", "a1-request", "reply:a1-request", false);
        }

        if (selected(selector, "TA-A2")) {
            ensureReady(actorUrl, callerUrl, "TA-A2", "ta-a2");
            assertCall(callerUrl, "TA-A2-send", "ta-a2", "a2-send", "sent", true);
            assertCall(callerUrl, "TA-A2-request", "ta-a2", "a2-request", "reply:a2-request", false);
        }

        if (selected(selector, "TA-A3")) {
            assertFailure(callerUrl, "TA-A3-before-bind", "ta-a3-missing", "ACTOR_ROUTE_NOT_FOUND", false);
            ensureReady(actorUrl, callerUrl, "TA-A3", "ta-a3");
            assertCall(callerUrl, "TA-A3-after-bind-send", "ta-a3", "a3-send", "sent", true);
            assertCall(callerUrl, "TA-A3-after-bind-request", "ta-a3", "a3-request", "reply:a3-request", false);
        }

        if (selected(selector, "TA-A4")) {
            ensureReady(actorUrl, callerUrl, "TA-A4", "ta-a4");
            assertCall(callerUrl, "TA-A4-disconnected-send", "ta-a4", "a4-send", "sent", true);
            assertCall(callerUrl, "TA-A4-disconnected-request", "ta-a4", "a4-request", "reply:a4-request", false);
            assertFailure(callerUrl, "TA-A4-destroyed", "ta-a4-destroyed", "ACTOR_ROUTE_NOT_FOUND", false);
        }

        if (selected(selector, "TA-B1")) {
            assertFailure(callerUrl, "TA-B1-missing-send", "missing-actor", "ACTOR_ROUTE_NOT_FOUND", true);
            assertFailure(callerUrl, "TA-B1-missing-request", "missing-actor", "ACTOR_ROUTE_NOT_FOUND", false);
        }

        if (selected(selector, "TA-B2")) {
            Contracts.ActorRefWire b2Ref = ensureRef(actorUrl, "TA-B2", "ta-b2");
            waitRefUntilReady(callerUrl, "TA-B2-ref-ready", b2Ref);
            Contracts.ActorRefWire staleB2Ref = new Contracts.ActorRefWire(
                b2Ref.nodeRidHex(), b2Ref.actorId(), b2Ref.generation() + 1);
            assertRefFailure(callerUrl, "TA-B2-stale-ref", staleB2Ref, "ACTOR_LOCATION_STALE", false);
            assertCall(callerUrl, "TA-B2-live-after-reresolve", "ta-b2", "b2-request", "reply:b2-request", false);
        }

        if (selected(selector, "TA-B3")) {
            Contracts.ActorRefWire b3Ref = ensureRef(actorUrl, "TA-B3", "ta-b3");
            waitRefUntilReady(callerUrl, "TA-B3-ref-ready", b3Ref);
            Contracts.ActorRefWire disconnectedB3Ref = new Contracts.ActorRefWire(
                systems.zlink.contracts.core.RoutingId.from("actor-missing-route").toHex(),
                b3Ref.actorId(), b3Ref.generation());
            assertRefFailure(callerUrl, "TA-B3-route-disconnected", disconnectedB3Ref, "ROUTE_NOT_CONNECTED", false);
            assertCall(callerUrl, "TA-B3-route-restored", "ta-b3", "b3-request", "reply:b3-request", false);
        }

        assertActorEvidence(actorUrl, selector);

        System.out.println("to-actor-messaging selector=" + selector + " result=passed");
        System.out.println("to-actor-messaging e2e result=passed");
    }

    private static void ensure(String actorUrl, String scenario, String actorId) {
        JsonHttp.postJson(
            actorUrl + "/ensure",
            new Contracts.ActorCallRequest(scenario, actorId, "ensure"),
            Contracts.ActorCallResponse.class);
    }

    private static Contracts.ActorRefWire ensureRef(String actorUrl, String scenario, String actorId) {
        return JsonHttp.postJson(
            actorUrl + "/ensure-ref",
            new Contracts.ActorCallRequest(scenario, actorId, "ensure"),
            Contracts.ActorRefWire.class);
    }

    private static void ensureReady(String actorUrl, String callerUrl, String scenario, String actorId) {
        ensure(actorUrl, scenario, actorId);
        waitUntilReady(callerUrl, scenario + "-ready", actorId);
    }

    private static void waitUntilReady(String callerUrl, String scenario, String actorId) {
        long deadline = System.nanoTime() + 5_000_000_000L;
        Contracts.ActorCallResponse response = null;
        while (System.nanoTime() < deadline) {
            response = call(callerUrl, scenario, actorId, "ready", false);
            if (response.errorKind() == null && "reply:ready".equals(response.result())) {
                return;
            }
            if (!isConvergenceError(response.errorKind())) {
                break;
            }
            sleepBriefly();
        }
        String error = response == null ? "no response" : response.errorKind();
        throw new IllegalStateException(scenario + " readiness failed " + error);
    }

    private static void waitRefUntilReady(
        String callerUrl,
        String scenario,
        Contracts.ActorRefWire actorRef) {
        long deadline = System.nanoTime() + 30_000_000_000L;
        Contracts.ActorCallResponse response = null;
        while (System.nanoTime() < deadline) {
            response = refCall(callerUrl, scenario, actorRef, "ready", false);
            if (response.errorKind() == null && "reply:ready".equals(response.result())) {
                return;
            }
            if (!isConvergenceError(response.errorKind()) && !"ROUTE_NOT_CONNECTED".equals(response.errorKind())) {
                break;
            }
            sleepBriefly();
        }
        String error = response == null ? "no response" : response.errorKind();
        throw new IllegalStateException(scenario + " ref readiness failed " + error);
    }

    private static boolean isConvergenceError(String errorKind) {
        return "REQUEST_FAILED".equals(errorKind) || "ACTOR_ROUTE_NOT_FOUND".equals(errorKind);
    }

    private static void sleepBriefly() {
        try {
            Thread.sleep(100);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("readiness wait interrupted", ex);
        }
    }

    private static void assertCall(
        String callerUrl,
        String scenario,
        String actorId,
        String value,
        String expected,
        boolean send) {
        Contracts.ActorCallResponse response = call(callerUrl, scenario, actorId, value, send);
        require(response.errorKind() == null, scenario + " unexpected error " + response.errorKind());
        require(expected.equals(response.result()), scenario + " expected " + expected + " got " + response.result());
    }

    private static void assertFailure(
        String callerUrl,
        String scenario,
        String actorId,
        String expectedKind,
        boolean send) {
        String endpoint = send ? "/send" : "/request";
        Contracts.ActorCallResponse response = JsonHttp.postJson(
            callerUrl + endpoint,
            new Contracts.ActorCallRequest(scenario, actorId, "missing"),
            Contracts.ActorCallResponse.class);
        require(expectedKind.equals(response.errorKind()),
            scenario + " expected " + expectedKind + " got " + response.errorKind());
    }

    private static void assertRefFailure(
        String callerUrl,
        String scenario,
        Contracts.ActorRefWire actorRef,
        String expectedKind,
        boolean send) {
        String endpoint = send ? "/send-ref" : "/request-ref";
        Contracts.ActorCallResponse response = JsonHttp.postJson(
            callerUrl + endpoint,
            new Contracts.ActorRefCallRequest(scenario, actorRef, "fault"),
            Contracts.ActorCallResponse.class);
        require(expectedKind.equals(response.errorKind()),
            scenario + " expected " + expectedKind + " got " + response.errorKind());
    }

    private static Contracts.ActorCallResponse call(
        String callerUrl,
        String scenario,
        String actorId,
        String value,
        boolean send) {
        String endpoint = send ? "/send" : "/request";
        return JsonHttp.postJson(
            callerUrl + endpoint,
            new Contracts.ActorCallRequest(scenario, actorId, value),
            Contracts.ActorCallResponse.class);
    }

    private static Contracts.ActorCallResponse refCall(
        String callerUrl,
        String scenario,
        Contracts.ActorRefWire actorRef,
        String value,
        boolean send) {
        String endpoint = send ? "/send-ref" : "/request-ref";
        return JsonHttp.postJson(
            callerUrl + endpoint,
            new Contracts.ActorRefCallRequest(scenario, actorRef, value),
            Contracts.ActorCallResponse.class);
    }

    private static void assertActorEvidence(String actorUrl, String selector) {
        List<Contracts.ActorEvidence> evidence = List.of(
            JsonHttp.getJson(actorUrl + "/evidence", Contracts.ActorEvidence[].class));
        if (selected(selector, "TA-A1")) {
            require(containsEvidence(evidence, "TA-A1-send", "ta-a1", "send"), "TA-A1 send evidence missing");
            require(containsEvidence(evidence, "TA-A1-request", "ta-a1", "request"), "TA-A1 request evidence missing");
        }
        if (selected(selector, "TA-A2")) {
            require(containsEvidence(evidence, "TA-A2-send", "ta-a2", "send"), "TA-A2 send evidence missing");
        }
        if (selected(selector, "TA-A3")) {
            require(containsEvidence(evidence, "TA-A3-after-bind-request", "ta-a3", "request"),
                "TA-A3 request evidence missing");
        }
        if (selected(selector, "TA-A4")) {
            require(containsEvidence(evidence, "TA-A4-disconnected-send", "ta-a4", "send"),
                "TA-A4 send evidence missing");
        }
        if (selected(selector, "TA-B1")) {
            require(!containsScenario(evidence, "TA-B1-missing-send"), "TA-B1 missing actor send reached actor");
            require(!containsScenario(evidence, "TA-B1-missing-request"), "TA-B1 missing actor request reached actor");
        }
        if (selected(selector, "TA-B2")) {
            require(containsEvidence(evidence, "TA-B2-live-after-reresolve", "ta-b2", "request"),
                "TA-B2 live follow-up evidence missing");
            require(!containsScenario(evidence, "TA-B2-stale-ref"), "TA-B2 stale ref reached actor");
        }
        if (selected(selector, "TA-B3")) {
            require(containsEvidence(evidence, "TA-B3-route-restored", "ta-b3", "request"),
                "TA-B3 restored route evidence missing");
            require(!containsScenario(evidence, "TA-B3-route-disconnected"),
                "TA-B3 disconnected route reached actor");
        }
    }

    private static boolean selected(String selector, String scenario) {
        return "all".equals(selector) || scenario.equals(selector);
    }

    private static boolean containsEvidence(
        List<Contracts.ActorEvidence> evidence,
        String scenario,
        String actorId,
        String kind) {
        return evidence.stream().anyMatch(item ->
            scenario.equals(item.scenario()) && actorId.equals(item.actorId()) && kind.equals(item.kind()));
    }

    private static boolean containsScenario(List<Contracts.ActorEvidence> evidence, String scenario) {
        return evidence.stream().anyMatch(item -> scenario.equals(item.scenario()));
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
