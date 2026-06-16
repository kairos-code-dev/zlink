package systems.zlink.samples.supportchat.server.support.application.assignment;

import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Queue;
import java.util.Set;

public final class AgentAvailabilityDirectory {
    private final Object gate = new Object();
    private final Queue<AvailableAgent> available = new ArrayDeque<>();
    private final Set<String> actorIds = new HashSet<>();

    public void setAvailable(String actorId, String displayName, boolean isAvailable) {
        synchronized (gate) {
            if (!isAvailable) {
                actorIds.remove(actorId);
                return;
            }
            if (actorIds.add(actorId)) {
                available.add(new AvailableAgent(actorId, displayName));
            }
        }
    }

    public AvailableAgent takeNext() {
        synchronized (gate) {
            AvailableAgent candidate;
            while ((candidate = available.poll()) != null) {
                if (actorIds.remove(candidate.actorId())) {
                    return candidate;
                }
            }
            return null;
        }
    }
}
