package systems.zlink.framework.locations.redis;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Base64;
import java.util.HexFormat;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkCreationOperationIdentity;

final class ZLinkRedisLocationKeys {
    private final String prefix;

    ZLinkRedisLocationKeys(String prefix) {
        this.prefix = prefix;
    }

    String rowHashKey(String tag, String rowKey) {
        if ("mesh-node".equals(tag)) {
            return rowHashKeyPrefix(tag) + sha256Hex(rowKey);
        }
        return rowHashKeyPrefix(tag) + rowKey;
    }

    String rowHashKeyPrefix(String tag) {
        if ("mesh-node".equals(tag)) {
            return domainBase() + ":descriptor:mesh:";
        }
        return prefix + ":row:" + tag + ":";
    }

    String generationKey(String tag, String rowKey) {
        return prefix + ":gen:" + tag + ":" + rowKey;
    }

    String kindIndexKey(String tag) {
        if ("mesh-node".equals(tag)) {
            return domainBase() + ":descriptor:mesh:index";
        }
        return prefix + ":keys:" + tag;
    }

    String ownerIndexKeyPrefix(String tag) {
        if ("mesh-node".equals(tag)) {
            return domainBase() + ":descriptor:mesh:owner:";
        }
        return prefix + ":own:" + tag + ":";
    }

    String meshNodeOwnerTokenIndexKey(
        String ownerId,
        long leaseGeneration) {
        return ownerIndexKeyPrefix("mesh-node")
            + sha256Hex(
                ownerId
                    + "\0"
                    + leaseGeneration);
    }

    String leaseKey(String ownerId) {
        return domainBase() + ":owner-lease:" + sha256Hex(ownerId);
    }

    String leaseKeyPrefix() {
        return domainBase() + ":owner-lease:";
    }

    String legacyLeaseKey(String ownerId) {
        return legacyLeaseKeyPrefix() + ownerId;
    }

    String legacyLeaseKeyPrefix() {
        return prefix + ":lease:";
    }

    String leaseIndexKey() {
        return domainBase() + ":owner-lease:index";
    }

    String leaseStateKey() {
        return domainBase() + ":owner-lease:legacy-state";
    }

    String leaseGenerationKey() {
        return counterKey();
    }

    String routingIdSlotGroupKey(String groupName) {
        return prefix + ":routing-id-slots:" + groupName;
    }

    String stampKey(String tag, String meshName) {
        if ("mesh-node".equals(tag)) {
            return meshName == null
                ? domainBase() + ":descriptor:mesh:stamp"
                : domainBase() + ":descriptor:mesh:stamp:"
                    + encode(meshName);
        }
        return meshName == null ? prefix + ":stamp:" + tag : prefix + ":stamp:" + tag + ":" + meshName;
    }

    String authorityRowKey(String authorityKey) {
        return domainBase() + ":authority:current:"
            + sha256Hex(authorityKey);
    }

    String authorityRowKeyPrefix() {
        return domainBase() + ":authority:current:";
    }

    String authorityIndexKey() {
        return domainBase() + ":authority:key-index";
    }

    String authorityRevisionKey() {
        return counterKey();
    }

    String authorityObjectGenerationKey() {
        return counterKey();
    }

    String authorityOwnerGenerationKey() {
        return counterKey();
    }

    String creationKey(String reservationId) {
        return domainBase() + ":creation:"
            + reservationId.toLowerCase(java.util.Locale.ROOT);
    }

    String creationTerminalKey(
        ZLinkCreationOperationIdentity operation) {
        byte[] sourceRid = operation.sourceNodeRid().toBytes();
        return prefix + ":{zlink-location-v3}:creation-terminal:"
            + sourceRid.length + ":"
            + HexFormat.of().formatHex(sourceRid) + ":"
            + operation.sourceLifecycleGeneration() + ":"
            + String.format(
                java.util.Locale.ROOT,
                "%016x%016x",
                operation.operationIdHigh(),
                operation.operationIdLow());
    }

    String authorityAggregateKey(
        java.util.UUID aggregateId,
        long generation) {
        return domainBase() + ":aggregate:"
            + uuidHex(aggregateId)
            + ":"
            + generation;
    }

    String relocationCapacityKey(String fence) {
        return domainBase() + ":relocation:"
            + fence.replace("-", "").toLowerCase(java.util.Locale.ROOT);
    }

    String placementCapacityStateKey() {
        return domainBase() + ":capacity:type:active";
    }

    String authorityMembershipsKey() {
        return domainBase() + ":membership:current";
    }

    String meshNodeDescriptorRowKey(
        systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey key) {
        return domainBase() + ":descriptor:mesh:"
            + sha256Hex(
                ZLinkRedisLocationKeyCodec.encodeMeshNodeKey(key));
    }

    String meshNodeDescriptorMetadataKey(
        systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey key) {
        return meshNodeDescriptorMetadataKeyPrefix()
            + sha256Hex(
                ZLinkRedisLocationKeyCodec.encodeMeshNodeKey(key));
    }

    String meshNodeDescriptorMetadataKeyPrefix() {
        return domainBase() + ":descriptor-admission:mesh:";
    }

    String meshNodeDescriptorStorageId(String encodedKey) {
        return sha256Hex(encodedKey);
    }

    String clientServerDescriptorRowKey(String encodedKey) {
        return domainBase() + ":descriptor:client-server:"
            + sha256Hex(encodedKey);
    }

    String clientServerDescriptorMetadataKey(String encodedKey) {
        return domainBase() + ":descriptor-admission:client-server:"
            + sha256Hex(encodedKey);
    }

    String clientServerDescriptorIndexKey() {
        return domainBase() + ":descriptor:client-server:index";
    }

    String clientServerDescriptorChannelIndexKey(String channelName) {
        return domainBase() + ":descriptor:client-server:channel:"
            + sha256Hex(channelName);
    }

    String clientServerOwnerTokenIndexKey(
        String ownerId,
        long leaseGeneration) {
        return domainBase() + ":descriptor:client-server:owner:"
            + sha256Hex(ownerId + "\0" + leaseGeneration);
    }

    String clientServerDescriptorStampKey(String channelName) {
        return domainBase() + ":descriptor:client-server:stamp:"
            + sha256Hex(channelName);
    }

    String clientServerDescriptorGlobalStampKey() {
        return domainBase() + ":descriptor:client-server:stamp";
    }

    String fanoutPublisherDescriptorRowKey(String encodedKey) {
        return domainBase() + ":descriptor:fanout-publisher:"
            + sha256Hex(encodedKey);
    }

    String fanoutPublisherDescriptorMetadataKey(String encodedKey) {
        return domainBase() + ":descriptor-admission:fanout-publisher:"
            + sha256Hex(encodedKey);
    }

    String fanoutPublisherDescriptorIndexKey() {
        return domainBase() + ":descriptor:fanout-publisher:index";
    }

    String fanoutPublisherDescriptorChannelIndexKey(
        String channelName) {
        return domainBase()
            + ":descriptor:fanout-publisher:channel:"
            + sha256Hex(channelName);
    }

    String fanoutPublisherOwnerTokenIndexKey(
        String ownerId,
        long leaseGeneration) {
        return domainBase() + ":descriptor:fanout-publisher:owner:"
            + sha256Hex(ownerId + "\0" + leaseGeneration);
    }

    String fanoutPublisherDescriptorStampKey(String channelName) {
        return domainBase() + ":descriptor:fanout-publisher:stamp:"
            + sha256Hex(channelName);
    }

    String fanoutPublisherDescriptorGlobalStampKey() {
        return domainBase() + ":descriptor:fanout-publisher:stamp";
    }

    String encodedAuthorityKey(String authorityKey) {
        return HexFormat.of().formatHex(
            authorityKey.getBytes(StandardCharsets.UTF_8));
    }

    String decodeAuthorityKey(String encoded) {
        return new String(
            HexFormat.of().parseHex(encoded),
            StandardCharsets.UTF_8);
    }

    String schemaKey() {
        return domainBase() + ":schema";
    }

    String counterKey() {
        return domainBase() + ":counter";
    }

    String authorityHistoryKey(String authorityKey) {
        return domainBase() + ":authority:history:"
            + sha256Hex(authorityKey);
    }

    String authorityHistoryRevisionsKey(String authorityKey) {
        return domainBase() + ":authority:history-revisions:"
            + sha256Hex(authorityKey);
    }

    String authorityIndexGcKey() {
        return domainBase() + ":authority:index-gc";
    }

    String membershipHistoryKey(String authorityKey) {
        return domainBase() + ":membership:history:"
            + sha256Hex(authorityKey);
    }

    String membershipHistoryRevisionsKey(String authorityKey) {
        return domainBase() + ":membership:history-revisions:"
            + sha256Hex(authorityKey);
    }

    String capacityNodeActiveKey() {
        return domainBase() + ":capacity:node:active";
    }

    String capacityNodePendingKey() {
        return domainBase() + ":capacity:node:pending";
    }

    String capacityTypeActiveKey() {
        return domainBase() + ":capacity:type:active";
    }

    String capacityTypePendingKey() {
        return domainBase() + ":capacity:type:pending";
    }

    String scanKey(java.util.UUID scanId) {
        return domainBase() + ":scan:" + uuidHex(scanId);
    }

    String scansExpiryKey() {
        return domainBase() + ":scans:expiry";
    }

    String scansWatermarkKey() {
        return domainBase() + ":scans:watermark";
    }

    private String domainBase() {
        return prefix + ":{zlink-location-v1}";
    }

    private static String encode(String value) {
        return Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(value.getBytes(StandardCharsets.UTF_8));
    }

    private static String sha256Hex(String value) {
        try {
            return HexFormat.of().formatHex(
                MessageDigest.getInstance("SHA-256").digest(
                    value.getBytes(StandardCharsets.UTF_8)));
        } catch (NoSuchAlgorithmException exception) {
            throw new IllegalStateException(
                "SHA-256 is required by the Redis location schema",
                exception);
        }
    }

    private static String uuidHex(java.util.UUID value) {
        return value.toString().replace("-", "")
            .toLowerCase(java.util.Locale.ROOT);
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
