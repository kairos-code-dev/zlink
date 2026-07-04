package systems.zlink.e2e.registrymessaging.consumer.Endpoints;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

@RestController
public final class ConsumerEndpoints {
    private final ZLinkClient client;
    private final ZLinkFrameworkLifecycle lifecycle;

    public ConsumerEndpoints(
        ZLinkClient client,
        ZLinkFrameworkLifecycle lifecycle) {
        this.client = client;
        this.lifecycle = lifecycle;
    }

    @GetMapping("/health")
    public java.util.Map<String, String> health() {
        return java.util.Map.of("status", "ready");
    }

    @GetMapping("/locations/peers")
    public List<java.util.Map<String, Object>> peers() {
        return lifecycle.monitoringLocationRuntimeQuery().listPeerLocationsAsync(new ZLinkPeerLocationFilter(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                Contracts.API_CHANNEL,
                ZLinkLocationRole.ROUTER,
                null,
                null))
            .toCompletableFuture()
            .join()
            .stream()
            .map(peer -> java.util.Map.<String, Object>of(
                "meshName", peer.meshName(),
                "role", peer.role().name(),
                "nodeRid", peer.nodeRid().toString(),
                "endpoint", peer.endpoint(),
                "ownerId", peer.ownerId()))
            .toList();
    }

    @PostMapping("/profile/request")
    public Contracts.ProfileRes profileRequest(@RequestBody Contracts.ProfileReq request) {
        return requestProfile(request, Duration.ofSeconds(5));
    }

    @PostMapping("/workflow/request")
    public Contracts.WorkflowRes workflowRequest(@RequestBody Contracts.WorkflowReq request) {
        return client.requestToChannel(Contracts.WORKFLOW_CHANNEL, request)
            .packetName("WorkflowReq")
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.WorkflowRes.class);
    }

    @PostMapping("/profile/batch-request")
    public List<Contracts.ProfileRes> profileBatch(@RequestBody List<Contracts.ProfileReq> requests) {
        List<Contracts.ProfileRes> replies = new ArrayList<>(requests.size());
        for (Contracts.ProfileReq request : requests) {
            replies.add(requestProfile(request, Duration.ofSeconds(5)));
        }
        return replies;
    }

    @PostMapping("/profile/slow-request")
    public Contracts.RequestFailureRes slowRequest(@RequestBody Contracts.ProfileReq request) {
        return requestFailure(request, Duration.ofMillis(100));
    }

    @PostMapping("/profile/missing-request")
    public Contracts.RequestFailureRes missingRequest(@RequestBody Contracts.ProfileReq request) {
        try {
            client.requestToChannel(Contracts.API_CHANNEL, request)
                .packetName("MissingProfileReq")
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.ProfileRes.class);
            return new Contracts.RequestFailureRes(false, "");
        } catch (RuntimeException error) {
            return new Contracts.RequestFailureRes(true, error.getClass().getSimpleName());
        }
    }

    @PostMapping("/profile/missing-command")
    public java.util.Map<String, String> missingCommand(@RequestBody Contracts.ProfileMsg command) {
        client.sendToChannel(Contracts.API_CHANNEL, command)
            .packetName("MissingProfileMsg")
            .submit();
        return java.util.Map.of("status", "sent");
    }

    @PostMapping("/profile/payload")
    public Contracts.PayloadRes payload(@RequestBody Contracts.PayloadReq request) {
        return client.requestToChannel(Contracts.API_CHANNEL, request)
            .packetName("PayloadReq")
            .timeout(Duration.ofSeconds(10))
            .await(Contracts.PayloadRes.class);
    }

    @PostMapping("/profile/backpressure/reset")
    public java.util.Map<String, String> backpressureReset() {
        return java.util.Map.of("status", "ready");
    }

    @PostMapping("/profile/backpressure/send")
    public Contracts.BackpressureRes backpressureSend(@RequestBody Contracts.ProfileMsg command) {
        client.sendToChannel(Contracts.API_CHANNEL, command)
            .packetName("ProfileMsg")
            .submit();
        return new Contracts.BackpressureRes("Submitted");
    }

    private Contracts.ProfileRes requestProfile(
        Contracts.ProfileReq request,
        Duration timeout) {
        return client.requestToChannel(Contracts.API_CHANNEL, request)
            .packetName("ProfileReq")
            .timeout(timeout)
            .await(Contracts.ProfileRes.class);
    }

    private Contracts.RequestFailureRes requestFailure(
        Contracts.ProfileReq request,
        Duration timeout) {
        try {
            requestProfile(request, timeout);
            return new Contracts.RequestFailureRes(false, "");
        } catch (RuntimeException error) {
            return new Contracts.RequestFailureRes(true, error.getClass().getSimpleName());
        }
    }
}
