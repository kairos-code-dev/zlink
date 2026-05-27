/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.sockets.AutoHwmRecalcReason;
import systems.zlink.contracts.eventing.MonitorSnapshot;
import systems.zlink.contracts.eventing.MonitorSocket;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.SocketType;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeSocketOwner;
import systems.zlink.contracts.service.spot.SpotNodeSocketSnapshotEntry;
import java.util.Locale;

final class PerfAutoHwm {
    private PerfAutoHwm() {
    }

    static void printSingleMonitor(PerfUtil.Config config,
                                   MonitorSocket monitor, String component,
                                   SocketType socketType) {
        MonitorSnapshot snapshot = monitor.snapshot();
        if (!visible(snapshot)) {
            return;
        }
        System.out.println("AUTO_HWM_DETAIL"
            + ",pattern=" + config.pattern()
            + ",transport=" + config.transport()
            + ",component=" + component
            + ",msg_size=" + config.size()
            + ",owner=socket"
            + ",owner_id=0"
            + ",socket=" + component
            + ",socket_type=" + socketTypeName(socketType)
            + ",role=" + roleName(snapshot.autoHwmRole())
            + ",sndhwm=" + snapshot.autoHwmAppliedSndHwm()
            + ",rcvhwm=" + snapshot.autoHwmAppliedRcvHwm()
            + ",effective_message_bytes="
            + snapshot.autoHwmEffectiveMessageBytes()
            + ",effective_sndbuf=" + snapshot.autoHwmAppliedSndBuffer()
            + ",effective_rcvbuf=" + snapshot.autoHwmAppliedRcvBuffer()
            + ",socket_message_slots="
            + snapshot.autoHwmSocketMessageSlots());
    }

    static void printSingleSpotNode(PerfUtil.Config config, SpotNode node,
                                    String component) {
        for (SpotNodeSocketSnapshotEntry entry : node.internalSocketsSnapshot()) {
            if (!entry.autoHwmVisible()) {
                continue;
            }
            MonitorSnapshot snapshot = entry.snapshot();
            if (snapshot.autoHwmAppliedSndHwm() <= 0
                && snapshot.autoHwmAppliedRcvHwm() <= 0) {
                continue;
            }
            System.out.println("AUTO_HWM_DETAIL"
                + ",pattern=" + config.pattern()
                + ",transport=" + config.transport()
                + ",component=" + component
                + ",msg_size=" + config.size()
                + ",owner=" + ownerName(entry.owner())
                + ",owner_id=" + entry.ownerId()
                + ",socket=" + entry.socketName()
                + ",socket_type=" + socketTypeName(entry.socketType())
                + ",role=" + roleName(snapshot.autoHwmRole())
                + ",sndhwm=" + snapshot.autoHwmAppliedSndHwm()
                + ",rcvhwm=" + snapshot.autoHwmAppliedRcvHwm()
                + ",effective_message_bytes="
                + snapshot.autoHwmEffectiveMessageBytes()
                + ",effective_sndbuf=" + snapshot.autoHwmAppliedSndBuffer()
                + ",effective_rcvbuf=" + snapshot.autoHwmAppliedRcvBuffer()
                + ",socket_message_slots="
                + snapshot.autoHwmSocketMessageSlots());
        }
    }

    static void printMultiSocket(PerfUtil.Config config, Socket socket,
                                 String component, String label,
                                 SocketType socketType) {
        try (MonitorSocket monitor = socket.monitorOpen()) {
            printMultiMonitor(config, monitor, component, label, socketType);
        }
    }

    static void printMultiMonitor(PerfUtil.Config config, MonitorSocket monitor,
                                  String component, String label,
                                  SocketType socketType) {
        MonitorSnapshot snapshot = monitor.snapshot();
        if (!visible(snapshot)) {
            return;
        }
        System.out.println("AUTO_HWM_DETAIL"
            + ",pattern=MULTI_" + config.pattern()
            + ",transport=" + config.transport()
            + ",component=" + component
            + ",label=" + label
            + ",socket_type=" + socketTypeName(socketType)
            + ",msg_size=" + config.size()
            + ",source=monitor_snapshot"
            + ",enabled=" + (snapshot.autoHwmEnabled() ? 1 : 0)
            + ",role=" + roleName(snapshot.autoHwmRole())
            + ",role_id=" + snapshot.autoHwmRole()
            + ",profile=" + profileName(snapshot.autoHwmProfile())
            + ",profile_id=" + profileId(snapshot.autoHwmProfile())
            + ",policy_class=" + policyClassName(snapshot.autoHwmPolicyClass())
            + ",policy_class_id=" + snapshot.autoHwmPolicyClass()
            + ",unit_budget_bytes=" + snapshot.autoHwmUnitBudgetBytes()
            + ",size_cap=" + snapshot.autoHwmSizeCap()
            + ",sndhwm=" + hwmDisplay(snapshot, socketType, true)
            + ",rcvhwm=" + hwmDisplay(snapshot, socketType, false)
            + ",socket_message_slots=" + snapshot.autoHwmSocketMessageSlots()
            + ",effective_message_bytes="
            + snapshot.autoHwmEffectiveMessageBytes()
            + ",effective_sndbuf=" + bufferDisplay(snapshot, socketType, true)
            + ",effective_rcvbuf=" + bufferDisplay(snapshot, socketType, false)
            + ",last_recalc_ms=" + snapshot.autoHwmLastRecalcMs()
            + ",last_recalc_reason="
            + recalcReasonName(snapshot.autoHwmLastRecalcReason())
            + ",send_blocked_ratio_ppm="
            + snapshot.autoHwmSendBlockedRatioPpm()
            + ",deferred_sndhwm=" + snapshot.autoHwmDeferredSndHwm()
            + ",deferred_rcvhwm=" + snapshot.autoHwmDeferredRcvHwm());
    }

    static void printMultiSpotNode(PerfUtil.Config config, SpotNode node,
                                   String component) {
        for (SpotNodeSocketSnapshotEntry entry : node.internalSocketsSnapshot()) {
            if (!entry.autoHwmVisible()) {
                continue;
            }
            MonitorSnapshot snapshot = entry.snapshot();
            if (!visible(snapshot)) {
                continue;
            }
            System.out.println("AUTO_HWM_DETAIL"
                + ",pattern=MULTI_" + config.pattern()
                + ",transport=" + config.transport()
                + ",component=" + component
                + ",label=" + entry.socketName()
                + ",owner=" + ownerName(entry.owner())
                + ",owner_id=" + entry.ownerId()
                + ",socket=" + entry.socketName()
                + ",socket_type=" + socketTypeName(entry.socketType())
                + ",msg_size=" + config.size()
                + ",source=spotnode_snapshot"
                + ",enabled=" + (snapshot.autoHwmEnabled() ? 1 : 0)
                + ",role=" + roleName(snapshot.autoHwmRole())
                + ",role_id=" + snapshot.autoHwmRole()
                + ",profile=" + profileName(snapshot.autoHwmProfile())
                + ",profile_id=" + profileId(snapshot.autoHwmProfile())
                + ",policy_class="
                + policyClassName(snapshot.autoHwmPolicyClass())
                + ",policy_class_id=" + snapshot.autoHwmPolicyClass()
                + ",unit_budget_bytes=" + snapshot.autoHwmUnitBudgetBytes()
                + ",size_cap=" + snapshot.autoHwmSizeCap()
                + ",scope=" + (entry.owner() == SpotNodeSocketOwner.NODE
                    ? "shared" : "per-spot")
                + ",sndhwm=" + hwmDisplay(snapshot, entry.socketType(), true)
                + ",rcvhwm=" + hwmDisplay(snapshot, entry.socketType(), false)
                + ",socket_message_slots="
                + snapshot.autoHwmSocketMessageSlots()
                + ",effective_message_bytes="
                + snapshot.autoHwmEffectiveMessageBytes()
                + ",effective_sndbuf="
                + bufferDisplay(snapshot, entry.socketType(), true)
                + ",effective_rcvbuf="
                + bufferDisplay(snapshot, entry.socketType(), false)
                + ",last_recalc_reason="
                + recalcReasonName(snapshot.autoHwmLastRecalcReason()));
        }
    }

    private static boolean visible(MonitorSnapshot snapshot) {
        return snapshot.autoHwmAppliedSndHwm() > 0
            || snapshot.autoHwmAppliedRcvHwm() > 0
            || snapshot.autoHwmEffectiveMessageBytes() > 0
            || snapshot.autoHwmSocketMessageSlots() > 0;
    }

    private static String roleName(int role) {
        return switch (role) {
            case 1 -> "control";
            case 2 -> "routed";
            case 3 -> "fanout";
            case 4 -> "recv_ingress";
            case 5 -> "spot_data";
            case 6 -> "peer_queue";
            case 7 -> "stream";
            default -> "none";
        };
    }

    private static String policyClassName(int policyClass) {
        return switch (policyClass) {
            case 1 -> "fanout";
            case 2 -> "spot_data";
            case 3 -> "recv_ingress";
            case 4 -> "routed";
            case 5 -> "peer_queue";
            case 6 -> "stream";
            case 7 -> "control";
            default -> "none";
        };
    }

    private static String profileName(systems.zlink.contracts.sockets.AutoHwmProfile profile) {
        return switch (profile) {
            case COMPACT -> "compact";
            case LOW_LATENCY -> "low_latency";
            case BALANCED -> "balanced";
            case THROUGHPUT -> "throughput";
        };
    }

    private static int profileId(systems.zlink.contracts.sockets.AutoHwmProfile profile) {
        return switch (profile) {
            case COMPACT -> 0;
            case LOW_LATENCY -> 1;
            case BALANCED -> 2;
            case THROUGHPUT -> 3;
        };
    }

    private static String recalcReasonName(
      systems.zlink.contracts.sockets.AutoHwmRecalcReason reason) {
        return switch (reason) {
            case INITIAL -> "initial";
            case ROLE_CHANGE -> "role_change";
            case POLICY_TOGGLE -> "policy_toggle";
            case REFRESH -> "refresh";
            case DEFERRED_SHRINK -> "deferred_shrink";
            default -> "none";
        };
    }

    private static String ownerName(SpotNodeSocketOwner owner) {
        return switch (owner) {
            case NODE -> "node";
            case SPOT -> "spot";
            default -> "unknown";
        };
    }

    private static String socketTypeName(SocketType type) {
        return type.name().toLowerCase(Locale.ROOT);
    }

    private static String hwmDisplay(MonitorSnapshot snapshot,
                                     SocketType socketType,
                                     boolean sendSide) {
        if (!sideVisible(socketType, snapshot.autoHwmRole(), sendSide)) {
            return "-";
        }
        return Integer.toString(sendSide
            ? snapshot.autoHwmAppliedSndHwm()
            : snapshot.autoHwmAppliedRcvHwm());
    }

    private static String bufferDisplay(MonitorSnapshot snapshot,
                                        SocketType socketType,
                                        boolean sendSide) {
        if (!sideVisible(socketType, snapshot.autoHwmRole(), sendSide)) {
            return "0";
        }
        return Integer.toString(sendSide
            ? snapshot.autoHwmAppliedSndBuffer()
            : snapshot.autoHwmAppliedRcvBuffer());
    }

    private static boolean sideVisible(SocketType socketType, int role,
                                       boolean sendSide) {
        String roleName = roleName(role);
        if (sendSide
            && (socketType == SocketType.SUB || socketType == SocketType.XSUB)
            && ("recv_ingress".equals(roleName) || "control".equals(roleName))) {
            return false;
        }
        return sendSide
            || !((socketType == SocketType.PUB || socketType == SocketType.XPUB)
            && ("spot_data".equals(roleName) || "control".equals(roleName)));
    }
}
