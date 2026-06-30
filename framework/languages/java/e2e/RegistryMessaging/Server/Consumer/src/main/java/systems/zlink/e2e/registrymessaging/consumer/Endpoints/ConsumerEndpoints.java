package systems.zlink.e2e.registrymessaging.consumer.Endpoints;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkClient;

@RestController
public final class ConsumerEndpoints {
    private final ZLinkClient client;

    public ConsumerEndpoints(ZLinkClient client) {
        this.client = client;
    }

    @GetMapping("/health")
    public java.util.Map<String, String> health() {
        return java.util.Map.of("status", "ready");
    }

    @PostMapping("/profile/request")
    public Contracts.ProfileReply profileRequest(@RequestBody Contracts.ProfileRequest request) {
        return requestProfile(request, Duration.ofSeconds(5));
    }

    @PostMapping("/profile/batch-request")
    public List<Contracts.ProfileReply> profileBatch(@RequestBody List<Contracts.ProfileRequest> requests) {
        List<Contracts.ProfileReply> replies = new ArrayList<>(requests.size());
        for (Contracts.ProfileRequest request : requests) {
            replies.add(requestProfile(request, Duration.ofSeconds(5)));
        }
        return replies;
    }

    @PostMapping("/profile/slow-request")
    public Contracts.RequestFailureResult slowRequest(@RequestBody Contracts.ProfileRequest request) {
        return requestFailure(request, Duration.ofMillis(100));
    }

    @PostMapping("/profile/missing-request")
    public Contracts.RequestFailureResult missingRequest(@RequestBody Contracts.ProfileRequest request) {
        try {
            client.requestToChannel(Contracts.API_CHANNEL, request)
                .packetName("MissingProfileRequest")
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.ProfileReply.class);
            return new Contracts.RequestFailureResult(false, "");
        } catch (RuntimeException error) {
            return new Contracts.RequestFailureResult(true, error.getClass().getSimpleName());
        }
    }

    @PostMapping("/profile/missing-command")
    public java.util.Map<String, String> missingCommand(@RequestBody Contracts.ProfileCommand command) {
        client.sendToChannel(Contracts.API_CHANNEL, command)
            .packetName("MissingProfileCommand")
            .await();
        return java.util.Map.of("status", "sent");
    }

    @PostMapping("/profile/payload")
    public Contracts.PayloadReply payload(@RequestBody Contracts.PayloadRequest request) {
        return client.requestToChannel(Contracts.API_CHANNEL, request)
            .packetName("PayloadRequest")
            .timeout(Duration.ofSeconds(10))
            .await(Contracts.PayloadReply.class);
    }

    @PostMapping("/profile/backpressure/reset")
    public java.util.Map<String, String> backpressureReset() {
        return java.util.Map.of("status", "ready");
    }

    @PostMapping("/profile/backpressure/send")
    public Contracts.BackpressureResult backpressureSend(@RequestBody Contracts.ProfileCommand command) {
        try {
            client.sendToChannel(Contracts.API_CHANNEL, command)
                .packetName("ProfileCommand")
                .submit()
                .toCompletableFuture()
                .get(750, TimeUnit.MILLISECONDS);
            return new Contracts.BackpressureResult("Accepted");
        } catch (Exception expected) {
            return new Contracts.BackpressureResult("BoundedFailure");
        }
    }

    private Contracts.ProfileReply requestProfile(
        Contracts.ProfileRequest request,
        Duration timeout) {
        return client.requestToChannel(Contracts.API_CHANNEL, request)
            .packetName("ProfileRequest")
            .timeout(timeout)
            .await(Contracts.ProfileReply.class);
    }

    private Contracts.RequestFailureResult requestFailure(
        Contracts.ProfileRequest request,
        Duration timeout) {
        try {
            requestProfile(request, timeout);
            return new Contracts.RequestFailureResult(false, "");
        } catch (RuntimeException error) {
            return new Contracts.RequestFailureResult(true, error.getClass().getSimpleName());
        }
    }
}
