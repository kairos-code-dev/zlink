package systems.zlink.framework.runtime.locations;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocation;

final class ZLinkAutoConnectPlanner {
    private ZLinkAutoConnectPlanner() {
    }

    record Local(
        ZLinkLocationAutoConnectType type,
        String meshName,
        ZLinkLocationRole role,
        RoutingId nodeRid,
        String endpoint) {
    }

    record Target(
        String key,
        RoutingId nodeRid,
        ZLinkLocationRole role,
        String endpoint,
        Map<String, String> metadata,
        String ownerId) {
    }

    static boolean isRoleAllowed(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role) {
        return switch (type) {
            case ROUTE_MESH -> role == ZLinkLocationRole.ROUTER;
            case CLIENT_SERVER -> role == ZLinkLocationRole.ROUTER
                || role == ZLinkLocationRole.DEALER;
            case DEALER_MESH -> role == ZLinkLocationRole.DEALER;
            case FANOUT -> role == ZLinkLocationRole.PUB || role == ZLinkLocationRole.SUB;
            case SPOT_MESH -> role == ZLinkLocationRole.SPOT
                || role == ZLinkLocationRole.ROUTER;
            default -> false;
        };
    }

    static Map<String, Target> computeDesired(Local local, List<ZLinkPeerLocation> peers) {
        Map<String, Target> desired = new HashMap<>();
        for (ZLinkPeerLocation peer : peers) {
            if (peer.autoConnectType() != local.type()
                || !peer.meshName().equals(local.meshName())
                || !isRoleAllowed(local.type(), peer.role())
                || peer.endpoint() == null
                || peer.endpoint().isBlank()) {
                continue;
            }
            if (isSelf(local, peer) || !shouldDial(local, peer)) {
                continue;
            }
            Target target = new Target(
                targetKeyOf(peer),
                peer.nodeRid(),
                peer.role(),
                peer.endpoint(),
                peer.metadata(),
                peer.ownerId());
            desired.put(target.key(), target);
        }
        return desired;
    }

    private static String targetKeyOf(ZLinkPeerLocation peer) {
        String identity = hasRid(peer.nodeRid())
            ? peer.nodeRid().toHex()
            : peer.endpoint();
        return peer.role().name().toLowerCase(java.util.Locale.ROOT) + "|" + identity;
    }

    private static boolean isSelf(Local local, ZLinkPeerLocation peer) {
        if (hasRid(local.nodeRid()) && hasRid(peer.nodeRid())
            && local.nodeRid().equals(peer.nodeRid())) {
            return true;
        }
        return peer.endpoint().equals(local.endpoint());
    }

    private static boolean shouldDial(Local local, ZLinkPeerLocation peer) {
        return switch (local.type()) {
            case ROUTE_MESH -> local.role() == ZLinkLocationRole.ROUTER
                && peer.role() == ZLinkLocationRole.ROUTER
                && localIsInitiator(local, peer);
            case CLIENT_SERVER -> local.role() == ZLinkLocationRole.DEALER
                && peer.role() == ZLinkLocationRole.ROUTER;
            case DEALER_MESH -> local.role() == ZLinkLocationRole.DEALER
                && peer.role() == ZLinkLocationRole.DEALER
                && localIsInitiator(local, peer);
            case FANOUT -> local.role() == ZLinkLocationRole.SUB
                && peer.role() == ZLinkLocationRole.PUB;
            case SPOT_MESH -> local.role() == ZLinkLocationRole.SPOT
                && peer.role() == ZLinkLocationRole.SPOT;
            default -> false;
        };
    }

    private static boolean localIsInitiator(Local local, ZLinkPeerLocation peer) {
        if (local.endpoint() == null || local.endpoint().isBlank()) {
            return true;
        }
        if (hasRid(local.nodeRid()) && hasRid(peer.nodeRid())) {
            int byRid = local.nodeRid().toHex().compareTo(peer.nodeRid().toHex());
            if (byRid != 0) {
                return byRid < 0;
            }
        }
        return local.endpoint().compareTo(peer.endpoint()) < 0;
    }

    static boolean hasRid(RoutingId rid) {
        return rid != null && rid.size() > 0;
    }
}
