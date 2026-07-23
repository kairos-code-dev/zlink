package systems.zlink.framework.locations;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
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
        if (aggregateId.getMostSignificantBits() == 0L
            && aggregateId.getLeastSignificantBits() == 0L) {
            throw new IllegalArgumentException(
                "aggregateId must not be zero");
        }
        if (aggregateGeneration <= 0) {
            throw new IllegalArgumentException(
                "aggregateGeneration must be positive");
        }
        participants = List.copyOf(
            Objects.requireNonNull(participants, "participants"));
        if (participants.isEmpty() || participants.size() > 1024) {
            throw new IllegalArgumentException(
                "participants must contain 1..1024 entries");
        }
        inventoryDigest = Objects.requireNonNull(
            inventoryDigest,
            "inventoryDigest").clone();
        if (inventoryDigest.length != 32) {
            throw new IllegalArgumentException(
                "inventoryDigest must contain exactly 32 bytes");
        }
        targetReservations = List.copyOf(
            Objects.requireNonNull(
                targetReservations,
                "targetReservations"));
        if (targetReservations.size() > 1024
            || new HashSet<>(targetReservations).size()
                != targetReservations.size()) {
            throw new IllegalArgumentException(
                "targetReservations must be unique and contain at most 1024 entries");
        }
        Objects.requireNonNull(targetOwner, "targetOwner");
        byte[] previous = null;
        int newOwnerCount = 0;
        long encodedSize = 16L + Long.BYTES + inventoryDigest.length;
        for (ZLinkAggregateParticipant participant : participants) {
            Objects.requireNonNull(participant, "participant");
            byte[] current = participant.authorityKey()
                .getBytes(StandardCharsets.UTF_8);
            if (previous != null
                && Arrays.compareUnsigned(previous, current) >= 0) {
                throw new IllegalArgumentException(
                    "participants must be sorted by UTF-8 key bytes and unique");
            }
            previous = current;
            if (participant.ownerTransition()
                == ZLinkAuthorityGenerationTransition.NEW_OWNER) {
                newOwnerCount++;
            }
            encodedSize = Math.addExact(
                encodedSize,
                current.length
                    + participant.expectedStoreVersion()
                        .getBytes(StandardCharsets.UTF_8).length
                    + participant.authorityPayload().length
                    + participant.membershipMutation().length
                    + 32L);
        }
        if (targetReservations.size() != newOwnerCount) {
            throw new IllegalArgumentException(
                "each NEW_OWNER participant requires one capacity reservation");
        }
        if (encodedSize > 1024L * 1024L) {
            throw new IllegalArgumentException(
                "encoded aggregate request exceeds 1 MiB");
        }
    }

    @Override
    public byte[] inventoryDigest() {
        return inventoryDigest.clone();
    }
}
