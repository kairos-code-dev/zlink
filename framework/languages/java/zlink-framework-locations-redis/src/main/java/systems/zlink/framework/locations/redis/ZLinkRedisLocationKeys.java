package systems.zlink.framework.locations.redis;

import java.nio.charset.StandardCharsets;
import java.util.Base64;
import systems.zlink.framework.locations.ZLinkLocationKind;

final class ZLinkRedisLocationKeys {
    private final String prefix;

    ZLinkRedisLocationKeys(String prefix) {
        this.prefix = prefix;
    }

    String rowHashKey(String tag, String rowKey) {
        return rowHashKeyPrefix(tag) + rowKey;
    }

    String rowHashKeyPrefix(String tag) {
        return prefix + ":row:" + tag + ":";
    }

    String generationKey(String tag, String rowKey) {
        return prefix + ":gen:" + tag + ":" + rowKey;
    }

    String kindIndexKey(String tag) {
        return prefix + ":keys:" + tag;
    }

    String ownerIndexKeyPrefix(String tag) {
        return prefix + ":own:" + tag + ":";
    }

    String leaseKey(String ownerId) {
        return leaseKeyPrefix() + ownerId;
    }

    String leaseKeyPrefix() {
        return prefix + ":lease:";
    }

    String leaseIndexKey() {
        return prefix + ":leases";
    }

    String routingIdSlotGroupKey(String groupName) {
        return prefix + ":routing-id-slots:" + groupName;
    }

    String stampKey(String tag, String meshName) {
        return meshName == null ? prefix + ":stamp:" + tag : prefix + ":stamp:" + tag + ":" + meshName;
    }

    String authorityRowKey(String authorityKey) {
        return authorityRowKeyPrefix() + encode(authorityKey);
    }

    String authorityRowKeyPrefix() {
        return authorityBase() + "row:";
    }

    String authorityIndexKey() {
        return authorityBase() + "index";
    }

    String authorityRevisionKey() {
        return authorityBase() + "revision";
    }

    String authorityObjectGenerationKey() {
        return authorityBase() + "object-generation";
    }

    String authorityOwnerGenerationKey() {
        return authorityBase() + "owner-generation";
    }

    String authorityReservationKey(String authorityKey) {
        return authorityBase() + "reservation:" + encode(authorityKey);
    }

    String authorityAggregateKey(java.util.UUID aggregateId) {
        return authorityBase() + "aggregate:" + aggregateId;
    }

    String encodedAuthorityKey(String authorityKey) {
        return encode(authorityKey);
    }

    String decodeAuthorityKey(String encoded) {
        return new String(
            Base64.getUrlDecoder().decode(encoded),
            StandardCharsets.UTF_8);
    }

    private String authorityBase() {
        return prefix + ":{authority}:";
    }

    private static String encode(String value) {
        return Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(value.getBytes(StandardCharsets.UTF_8));
    }

    static String tagOf(ZLinkLocationKind kind) {
        return switch (kind) {
            case PEER -> "peer";
            case SPOT -> "spot";
            case ACTOR -> "actor";
            case ROUTE -> "route";
            case INVALID -> throw new IllegalArgumentException("invalid location kind");
        };
    }
}
