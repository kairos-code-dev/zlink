package systems.zlink.e2e.registrymessaging.provider.Endpoints;

import java.time.Duration;
import java.util.List;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.registrymessaging.provider.Infrastructure.ScenarioState;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRouteClient;

@RestController
public final class ProviderEndpoints {
    private final ScenarioState state;
    private final ZLinkClient client;
    private final ZLinkRouteClient routes;

    public ProviderEndpoints(
        ScenarioState state,
        ZLinkClient client,
        ZLinkRouteClient routes) {
        this.state = state;
        this.client = client;
        this.routes = routes;
    }

    @GetMapping("/health")
    public java.util.Map<String, String> health() {
        return java.util.Map.of("status", "ready", "rid", state.providerRid());
    }

    @GetMapping("/evidence")
    public List<String> evidence() {
        return state.lines();
    }

    @PostMapping("/evidence/clear")
    public java.util.Map<String, String> clearEvidence() {
        state.clear();
        return java.util.Map.of("status", "cleared");
    }

    @PostMapping("/evidence/wait")
    public List<String> waitEvidence(@RequestBody Contracts.EvidenceWaitRequest request) {
        long timeout = Math.max(1, Math.min(30000, request.timeoutMilliseconds()));
        return state.waitUntil(line -> line.contains(request.contains()), timeout);
    }

    @PostMapping("/profile/request")
    public Contracts.ProfileReply profileRequest(@RequestBody Contracts.ProfileRequest request) {
        return requestProfile(Contracts.API_CHANNEL, request, Duration.ofSeconds(5));
    }

    @PostMapping("/profile/manual")
    public Contracts.ProfileReply profileManual(@RequestBody Contracts.ProfileRequest request) {
        return requestProfile("registry.messaging.api.manual", request, Duration.ofSeconds(5));
    }

    @PostMapping("/profile/command")
    public java.util.Map<String, String> profileCommand(@RequestBody Contracts.ProfileCommand command) {
        client.sendToChannel(Contracts.API_CHANNEL, command)
            .packetName("ProfileCommand")
            .await();
        return java.util.Map.of("status", "sent");
    }

    @PostMapping("/profile/route/request")
    public Contracts.RoutePong routeRequest(@RequestBody Contracts.RoutePing request) {
        return routes.requestTo(Contracts.ROUTE_CHANNEL, RoutingId.from("api-b"), request)
            .packetName(Contracts.ROUTE_PACKET)
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.RoutePong.class);
    }

    @PostMapping("/profile/route/missing")
    public Contracts.RouteMissingResult routeMissing(@RequestBody Contracts.RoutePing request) {
        try {
            routes.requestTo(Contracts.ROUTE_CHANNEL, RoutingId.from("missing-rid"), request)
                .packetName(Contracts.ROUTE_PACKET)
                .timeout(Duration.ofMillis(300))
                .await(Contracts.RoutePong.class);
            return new Contracts.RouteMissingResult(false);
        } catch (RuntimeException expected) {
            return new Contracts.RouteMissingResult(true);
        }
    }

    private Contracts.ProfileReply requestProfile(
        String channelName,
        Contracts.ProfileRequest request,
        Duration timeout) {
        return client.requestToChannel(channelName, request)
            .packetName("ProfileRequest")
            .timeout(timeout)
            .await(Contracts.ProfileReply.class);
    }
}
