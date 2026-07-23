package systems.zlink.framework.locations;

import java.util.List;
import java.util.Objects;
import java.util.UUID;

public record ZLinkAggregatePrepareRequest(
    UUID aggregateId,
    long aggregateGeneration,
    List<ZLinkAggregateParticipant> participants,
    byte[] inventoryDigest,
    List<ZLinkRelocationCapacityFence> targetReservations,
    ZLinkLocationOwnerToken targetOwner) {
    public ZLinkAggregatePrepareRequest {
        Objects.requireNonNull(aggregateId, "aggregateId");
        participants = List.copyOf(participants);
        inventoryDigest = Objects.requireNonNull(
            inventoryDigest,
            "inventoryDigest").clone();
        targetReservations = List.copyOf(targetReservations);
        Objects.requireNonNull(targetOwner, "targetOwner");
    }

    @Override
    public byte[] inventoryDigest() {
        return inventoryDigest.clone();
    }
}
