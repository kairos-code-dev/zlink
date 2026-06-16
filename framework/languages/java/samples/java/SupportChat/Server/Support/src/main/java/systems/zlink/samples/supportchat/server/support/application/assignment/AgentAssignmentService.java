package systems.zlink.samples.supportchat.server.support.application.assignment;

public final class AgentAssignmentService {
    private final AgentAvailabilityDirectory availability;

    public AgentAssignmentService(AgentAvailabilityDirectory availability) {
        this.availability = availability;
    }

    public AvailableAgent assignNextAgent() {
        return availability.takeNext();
    }
}
