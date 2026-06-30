package systems.zlink.e2e.registrymessaging.registry.Endpoints;

import java.util.List;
import java.util.Map;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;
import systems.zlink.framework.registry.ZLinkRegistryQuery;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;

@RestController
public final class RegistryMessagingEndpoints {
    private final ZLinkRegistryQuery registry;

    public RegistryMessagingEndpoints(ZLinkRegistryQuery registry) {
        this.registry = registry;
    }

    @GetMapping("/health")
    public java.util.Map<String, String> health() {
        return java.util.Map.of("status", "ready");
    }

    @GetMapping("/registry/topology")
    public List<Map<String, Object>> topology() {
        return registry.topology().toCompletableFuture().join().stream()
            .map(this::toJson)
            .toList();
    }

    private Map<String, Object> toJson(ZLinkRegistryTopologyEntry entry) {
        return Map.of(
            "channelName", entry.channelName(),
            "routingId", entry.routingId().toString(),
            "endpoint", entry.endpoint(),
            "serviceRole", entry.serviceRole().toString(),
            "state", entry.state().toString());
    }
}
