package systems.zlink.e2e.registrymessaging.consumer.Endpoints;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.e2e.registrymessaging.shared.FailureEvidence;
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
    public CompletionStage<List<java.util.Map<String, Object>>> peers() {
        return lifecycle.monitoringLocationRuntimeQuery().listPeerLocations(new ZLinkPeerLocationFilter(
                ZLinkLocationAutoConnectType.CLIENT_SERVER,
                Contracts.API_CHANNEL,
                ZLinkLocationRole.ROUTER,
                null,
                null))
            .thenApply(peers -> peers.stream()
            .map(peer -> java.util.Map.<String, Object>of(
                "meshName", peer.meshName(),
                "role", peer.role().name(),
                "nodeRid", peer.nodeRid().toString(),
                "endpoint", peer.endpoint(),
                "ownerId", peer.ownerId()))
            .toList());
    }

    @PostMapping("/profile/request")
    public CompletionStage<Contracts.ProfileRes> profileRequest(@RequestBody Contracts.ProfileReq request) {
        return requestProfile(request, Duration.ofSeconds(5));
    }

    @PostMapping("/workflow/request")
    public CompletionStage<Contracts.WorkflowRes> workflowRequest(@RequestBody Contracts.WorkflowReq request) {
        return client.requestToChannel(Contracts.WORKFLOW_CHANNEL, request)
            .timeout(Duration.ofSeconds(5))
            .submit(Contracts.WorkflowRes.class);
    }

    @PostMapping("/profile/batch-request")
    public CompletionStage<List<Contracts.ProfileRes>> profileBatch(
        @RequestBody List<Contracts.ProfileReq> requests) {
        List<Contracts.ProfileRes> replies = new ArrayList<>(requests.size());
        CompletionStage<Void> sequence = CompletableFuture.completedFuture(null);
        for (Contracts.ProfileReq request : requests) {
            sequence = sequence.thenCompose(ignored -> requestProfile(request, Duration.ofSeconds(5))
                .thenAccept(replies::add));
        }
        return sequence.thenApply(ignored -> List.copyOf(replies));
    }

    @PostMapping("/profile/slow-request")
    public CompletionStage<Contracts.RequestFailureRes> slowRequest(@RequestBody Contracts.ProfileReq request) {
        return requestFailure(request, Duration.ofMillis(100));
    }

    @PostMapping("/profile/missing-request")
    public CompletionStage<Contracts.RequestFailureRes> missingRequest(
        @RequestBody Contracts.ProfileReq request) {
        return client.requestToChannel(
                Contracts.API_CHANNEL,
                new Contracts.MissingProfileReq(request.value()))
                .timeout(Duration.ofSeconds(5))
                .submit(Contracts.ProfileRes.class)
            .handle((ignored, error) -> FailureEvidence.from(error));
    }

    @PostMapping("/profile/missing-command")
    public java.util.Map<String, String> missingCommand(@RequestBody Contracts.ProfileMsg command) {
        client.sendToChannel(
            Contracts.API_CHANNEL,
            new Contracts.MissingProfileMsg(command.commandId())).submit();
        return java.util.Map.of("status", "sent");
    }

    @PostMapping("/profile/payload")
    public CompletionStage<Contracts.PayloadRes> payload(@RequestBody Contracts.PayloadReq request) {
        return client.requestToChannel(Contracts.API_CHANNEL, request)
            .timeout(Duration.ofSeconds(10))
            .submit(Contracts.PayloadRes.class);
    }

    @PostMapping("/profile/backpressure/reset")
    public java.util.Map<String, String> backpressureReset() {
        return java.util.Map.of("status", "ready");
    }

    @PostMapping("/profile/backpressure/send")
    public Contracts.BackpressureRes backpressureSend(@RequestBody Contracts.ProfileMsg command) {
        client.sendToChannel(Contracts.API_CHANNEL, command)
            .submit();
        return new Contracts.BackpressureRes("Submitted");
    }

    private CompletionStage<Contracts.ProfileRes> requestProfile(
        Contracts.ProfileReq request,
        Duration timeout) {
        return client.requestToChannel(Contracts.API_CHANNEL, request)
            .timeout(timeout)
            .submit(Contracts.ProfileRes.class);
    }

    private CompletionStage<Contracts.RequestFailureRes> requestFailure(
        Contracts.ProfileReq request,
        Duration timeout) {
        return requestProfile(request, timeout)
            .handle((ignored, error) -> FailureEvidence.from(error));
    }
}
