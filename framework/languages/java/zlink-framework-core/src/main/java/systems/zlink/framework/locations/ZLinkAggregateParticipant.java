package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkAggregateParticipant(
    String authorityKey,
    String expectedStoreVersion,
    ZLinkAuthorityGenerationTransition ownerTransition,
    byte[] authorityPayload,
    byte[] membershipMutation) {
    public ZLinkAggregateParticipant {
        Objects.requireNonNull(authorityKey, "authorityKey");
        Objects.requireNonNull(
            expectedStoreVersion,
            "expectedStoreVersion");
        Objects.requireNonNull(ownerTransition, "ownerTransition");
        authorityPayload = Objects.requireNonNull(
            authorityPayload,
            "authorityPayload").clone();
        membershipMutation = Objects.requireNonNull(
            membershipMutation,
            "membershipMutation").clone();
    }

    @Override
    public byte[] authorityPayload() {
        return authorityPayload.clone();
    }

    @Override
    public byte[] membershipMutation() {
        return membershipMutation.clone();
    }
}
