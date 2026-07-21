package systems.zlink.framework.locations.redis;

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
