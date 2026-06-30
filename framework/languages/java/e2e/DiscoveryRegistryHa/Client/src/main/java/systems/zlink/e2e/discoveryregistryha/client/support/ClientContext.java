package systems.zlink.e2e.discoveryregistryha.client.support;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.e2e.discoveryregistryha.shared.HttpSupport;
import systems.zlink.e2e.discoveryregistryha.shared.Wait;

public final class ClientContext {
    private static final ObjectMapper JSON = new ObjectMapper();

    private final ClientOptions options;

    public ClientContext(ClientOptions options) {
        this.options = options;
    }

    public DiscoveryApiResult runStandard() {
        waitForExpectedMembers();
        return requestUntilExpectedProviders();
    }

    public DiscoveryApiResult runWithoutMemberProbe() {
        return requestUntilExpectedProviders();
    }

    public DiscoveryApiResult runWithDeadRegistryProbe() {
        waitForExpectedMembers();
        DiscoveryApiResult result = requestUntilExpectedProviders();
        expectHttpFailure(options.deadHttpEndpoint());
        return result;
    }

    public DiscoveryApiResult runWithTopologyComparison() {
        DiscoveryApiResult result = runStandard();
        expectRemoteTopologyMatchesProbe();
        return result;
    }

    private void waitForExpectedMembers() {
        Set<String> expectedSet = new HashSet<>(options.expectedMemberRids());
        for (String probe : options.probeHttpEndpoints()) {
            Set<String> observed = Wait.until(
                Duration.ofSeconds(15),
                "probe " + probe + " missing expected members " + options.expectedMemberRids(),
                () -> {
                    Set<String> rids = memberRids(probe);
                    return rids.containsAll(expectedSet) ? rids : null;
                });
            ScenarioAssert.that(observed.containsAll(expectedSet),
                "probe " + probe + " missing expected members " + options.expectedMemberRids() + ": " + observed);
        }
    }

    private Set<String> memberRids(String probe) {
        try {
            JsonNode root = JSON.readTree(HttpSupport.get(probe, "/member-peers"));
            Set<String> rids = new HashSet<>();
            for (JsonNode entry : root) {
                String rid = entry.path("routingId").asText("");
                if (!rid.isBlank()) {
                    rids.add(rid);
                }
            }
            return rids;
        } catch (Exception error) {
            return Set.of();
        }
    }

    private DiscoveryApiResult requestUntilExpectedProviders() {
        String consumerEndpoint = options.consumerHttpEndpoint();
        if (consumerEndpoint == null || consumerEndpoint.isBlank()) {
            throw new IllegalStateException("consumer HTTP endpoint is required");
        }
        List<String> expected = options.expectedRids();
        Set<String> providers = new HashSet<>();
        for (int index = 0; index < 120 && !providers.containsAll(expected); index++) {
            Contracts.ProfileRequest request =
                new Contracts.ProfileRequest("msg-" + index, "marker-" + index);
            try {
                String body = HttpSupport.postJson(consumerEndpoint, "/profile/request/wait", request, JSON);
                Contracts.ProfileReply reply = JSON.readValue(body, Contracts.ProfileReply.class);
                ScenarioAssert.that(reply.value().equals("profile:msg-" + index), "reply payload mismatch");
                providers.add(reply.providerRid());
            } catch (Exception error) {
                throw new IllegalStateException("consumer request failed", error);
            }
        }
        ScenarioAssert.that(providers.containsAll(expected),
            "messaging did not reach expected providers " + expected + ": " + providers);
        return new DiscoveryApiResult(Set.copyOf(providers));
    }

    private void expectHttpFailure(String baseUrl) {
        if (baseUrl == null || baseUrl.isBlank()) {
            return;
        }
        try {
            HttpSupport.get(baseUrl, "/health");
            throw new IllegalStateException("dead registry probe unexpectedly responded: " + baseUrl);
        } catch (IllegalStateException error) {
            throw error;
        } catch (Exception expected) {
            // The stopped registry must not keep serving the probe endpoint.
        }
    }

    private void expectRemoteTopologyMatchesProbe() {
        Set<String> probeRids = topologyRids(options.topologyHttpEndpoint());
        Set<String> remoteRids = topologyRids(options.remoteTopologyHttpEndpoint());
        ScenarioAssert.that(remoteRids.equals(probeRids),
            "remote topology did not match probe: remote=" + remoteRids + " probe=" + probeRids);
    }

    private Set<String> topologyRids(String probe) {
        if (probe == null || probe.isBlank()) {
            throw new IllegalStateException("topology probe endpoint is required");
        }
        try {
            JsonNode root = JSON.readTree(HttpSupport.get(probe, "/topology-rids"));
            Set<String> rids = new HashSet<>();
            for (JsonNode entry : root) {
                String rid = entry.asText("");
                if (!rid.isBlank()) {
                    rids.add(rid);
                }
            }
            return rids;
        } catch (Exception error) {
            throw new IllegalStateException("topology probe query failed: " + probe, error);
        }
    }
}
