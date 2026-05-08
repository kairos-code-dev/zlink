/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.internal;

import systems.zlink.AutoHwmProfile;
import systems.zlink.AutoHwmRecalcReason;
import systems.zlink.MonitorEventType;
import systems.zlink.MonitorSourceKind;
import systems.zlink.PollEventFlag;
import systems.zlink.RidDuplicatePolicy;
import systems.zlink.SocketType;
import systems.zlink.SpotDispatchEvent;
import systems.zlink.SpotDispatchSubjectKind;
import systems.zlink.service.registry.AutoConnectType;
import systems.zlink.service.registry.RegistryState;
import systems.zlink.service.registry.ServiceEventSubjectKind;
import systems.zlink.service.registry.ServiceKind;
import systems.zlink.service.registry.ServiceRole;
import systems.zlink.service.registry.TopologySource;
import systems.zlink.service.registry.TopologyState;
import systems.zlink.service.spot.ActorAdmissionResult;
import systems.zlink.service.spot.ActorCreateStatus;
import systems.zlink.service.spot.SpotNodeMode;
import systems.zlink.service.spot.SpotNodeSocketOwner;
import systems.zlink.service.spot.SpotNodeState;
import systems.zlink.service.spot.SpotPeerSource;
import systems.zlink.service.spot.SpotPeerState;
import systems.zlink.service.spot.SpotRole;
import java.util.EnumSet;

/**
 * Non-exported numeric enum codec table shared by binding internals.
 */
public final class EnumCodecs {
    private EnumCodecs() {
    }

    public static int autoHwmProfileValue(AutoHwmProfile value) {
        return switch (value) {
            case COMPACT -> 0;
            case LOW_LATENCY -> 1;
            case BALANCED -> 2;
            case THROUGHPUT -> 3;
        };
    }

    public static AutoHwmProfile autoHwmProfileFromValue(int value) {
        return switch (value) {
            case 0 -> AutoHwmProfile.COMPACT;
            case 1 -> AutoHwmProfile.LOW_LATENCY;
            case 2 -> AutoHwmProfile.BALANCED;
            case 3 -> AutoHwmProfile.THROUGHPUT;
            default -> throw invalid("AutoHwmProfile", value);
        };
    }

    public static AutoHwmRecalcReason autoHwmRecalcReasonFromValue(int value) {
        return switch (value) {
            case 0 -> AutoHwmRecalcReason.NONE;
            case 1 -> AutoHwmRecalcReason.INITIAL;
            case 2 -> AutoHwmRecalcReason.ROLE_CHANGE;
            case 3 -> AutoHwmRecalcReason.POLICY_TOGGLE;
            case 4 -> AutoHwmRecalcReason.REFRESH;
            case 5 -> AutoHwmRecalcReason.DEFERRED_SHRINK;
            default -> throw invalid("AutoHwmRecalcReason", value);
        };
    }

    public static int autoHwmRecalcReasonValue(AutoHwmRecalcReason value) {
        return switch (value) {
            case NONE -> 0;
            case INITIAL -> 1;
            case ROLE_CHANGE -> 2;
            case POLICY_TOGGLE -> 3;
            case REFRESH -> 4;
            case DEFERRED_SHRINK -> 5;
        };
    }

    public static MonitorEventType monitorEventTypeFromValue(long value) {
        return switch ((int) value) {
            case 0x0001 -> MonitorEventType.CONNECTED;
            case 0x0002 -> MonitorEventType.CONNECT_DELAYED;
            case 0x0004 -> MonitorEventType.CONNECT_RETRIED;
            case 0x0008 -> MonitorEventType.LISTENING;
            case 0x0010 -> MonitorEventType.BIND_FAILED;
            case 0x0020 -> MonitorEventType.ACCEPTED;
            case 0x0040 -> MonitorEventType.ACCEPT_FAILED;
            case 0x0080 -> MonitorEventType.CLOSED;
            case 0x0100 -> MonitorEventType.CLOSE_FAILED;
            case 0x0200 -> MonitorEventType.DISCONNECTED;
            case 0x0400 -> MonitorEventType.MONITOR_STOPPED;
            case 0x0800 -> MonitorEventType.HANDSHAKE_FAILED_NO_DETAIL;
            case 0x1000 -> MonitorEventType.CONNECTION_READY;
            case 0x2000 -> MonitorEventType.HANDSHAKE_FAILED_PROTOCOL;
            case 0x4000 -> MonitorEventType.HANDSHAKE_FAILED_AUTH;
            case 0x8000 -> MonitorEventType.PEER_WEIGHT_CHANGED;
            case 0xFFFF -> MonitorEventType.ALL;
            default -> throw invalid("MonitorEventType", value);
        };
    }

    public static int monitorEventTypeValue(MonitorEventType value) {
        return switch (value) {
            case CONNECTED -> 0x0001;
            case CONNECT_DELAYED -> 0x0002;
            case CONNECT_RETRIED -> 0x0004;
            case LISTENING -> 0x0008;
            case BIND_FAILED -> 0x0010;
            case ACCEPTED -> 0x0020;
            case ACCEPT_FAILED -> 0x0040;
            case CLOSED -> 0x0080;
            case CLOSE_FAILED -> 0x0100;
            case DISCONNECTED -> 0x0200;
            case MONITOR_STOPPED -> 0x0400;
            case HANDSHAKE_FAILED_NO_DETAIL -> 0x0800;
            case CONNECTION_READY -> 0x1000;
            case HANDSHAKE_FAILED_PROTOCOL -> 0x2000;
            case HANDSHAKE_FAILED_AUTH -> 0x4000;
            case PEER_WEIGHT_CHANGED -> 0x8000;
            case ALL -> 0xFFFF;
        };
    }

    public static int monitorEventMask(MonitorEventType... values) {
        int mask = 0;
        for (MonitorEventType value : values) {
            mask |= monitorEventTypeValue(value);
        }
        return mask;
    }

    public static MonitorSourceKind monitorSourceKindFromValue(int value) {
        return switch (value) {
            case 1 -> MonitorSourceKind.SOCKET;
            case 3 -> MonitorSourceKind.SPOT_PUB;
            case 4 -> MonitorSourceKind.SPOT_SUB;
            default -> throw invalid("MonitorSourceKind", value);
        };
    }

    public static int monitorSourceKindValue(MonitorSourceKind value) {
        return switch (value) {
            case SOCKET -> 1;
            case SPOT_PUB -> 3;
            case SPOT_SUB -> 4;
        };
    }

    public static int pollEventFlagValue(PollEventFlag value) {
        return switch (value) {
            case POLLIN -> 1;
            case POLLOUT -> 2;
            case POLLERR -> 4;
            case POLLPRI -> 8;
        };
    }

    public static int pollEventMask(PollEventFlag... values) {
        int mask = 0;
        for (PollEventFlag value : values) {
            mask |= pollEventFlagValue(value);
        }
        return mask;
    }

    public static EnumSet<PollEventFlag> pollEventFlagsFromMask(int mask) {
        EnumSet<PollEventFlag> out = EnumSet.noneOf(PollEventFlag.class);
        for (PollEventFlag value : PollEventFlag.values()) {
            if ((mask & pollEventFlagValue(value)) != 0) {
                out.add(value);
            }
        }
        return out;
    }

    public static int ridDuplicatePolicyValue(RidDuplicatePolicy value) {
        return switch (value) {
            case REJECT -> 0;
            case HANDOVER -> 1;
        };
    }

    public static RidDuplicatePolicy ridDuplicatePolicyFromValue(int value) {
        return switch (value) {
            case 0 -> RidDuplicatePolicy.REJECT;
            case 1 -> RidDuplicatePolicy.HANDOVER;
            default -> throw invalid("RidDuplicatePolicy", value);
        };
    }

    public static int socketTypeValue(SocketType value) {
        return switch (value) {
            case ANY -> 0;
            case PAIR -> 0x1001;
            case PUB -> 0x1002;
            case SUB -> 0x1003;
            case DEALER -> 0x1004;
            case ROUTER -> 0x1005;
            case XPUB -> 0x1006;
            case XSUB -> 0x1007;
            case STREAM -> 0x1008;
        };
    }

    public static SocketType socketTypeFromValue(int value) {
        return switch (value) {
            case 0 -> SocketType.ANY;
            case 0x1001 -> SocketType.PAIR;
            case 0x1002 -> SocketType.PUB;
            case 0x1003 -> SocketType.SUB;
            case 0x1004 -> SocketType.DEALER;
            case 0x1005 -> SocketType.ROUTER;
            case 0x1006 -> SocketType.XPUB;
            case 0x1007 -> SocketType.XSUB;
            case 0x1008 -> SocketType.STREAM;
            default -> throw invalid("SocketType", value);
        };
    }

    public static SpotDispatchEvent spotDispatchEventFromValue(int value) {
        return switch (value) {
            case 1 -> SpotDispatchEvent.SUBSCRIBE_READABLE;
            case 2 -> SpotDispatchEvent.ROUTED_READABLE;
            case 3 -> SpotDispatchEvent.TIMER_READABLE;
            case 4 -> SpotDispatchEvent.CHANNEL_REPLY_READABLE;
            case 5 -> SpotDispatchEvent.ACTOR_READABLE;
            case 6 -> SpotDispatchEvent.ACTOR_JOIN_READABLE;
            default -> throw invalid("SpotDispatchEvent", value);
        };
    }

    public static int spotDispatchEventValue(SpotDispatchEvent value) {
        return switch (value) {
            case SUBSCRIBE_READABLE -> 1;
            case ROUTED_READABLE -> 2;
            case TIMER_READABLE -> 3;
            case CHANNEL_REPLY_READABLE -> 4;
            case ACTOR_READABLE -> 5;
            case ACTOR_JOIN_READABLE -> 6;
        };
    }

    public static SpotDispatchSubjectKind spotDispatchSubjectKindFromValue(
      int value) {
        return switch (value) {
            case 1 -> SpotDispatchSubjectKind.SPOT;
            case 2 -> SpotDispatchSubjectKind.TIMER;
            case 3 -> SpotDispatchSubjectKind.CHANNEL_DEALER;
            case 4 -> SpotDispatchSubjectKind.ACTOR;
            default -> throw invalid("SpotDispatchSubjectKind", value);
        };
    }

    public static int spotDispatchSubjectKindValue(
      SpotDispatchSubjectKind value) {
        return switch (value) {
            case SPOT -> 1;
            case TIMER -> 2;
            case CHANNEL_DEALER -> 3;
            case ACTOR -> 4;
        };
    }

    public static int autoConnectTypeValue(AutoConnectType value) {
        return switch (value) {
            case INVALID -> 0;
            case ROUTE_MESH -> 1;
            case CLIENT_SERVER -> 2;
            case DEALER_MESH -> 3;
            case FANOUT -> 4;
            case SPOT_MESH -> 5;
        };
    }

    public static AutoConnectType autoConnectTypeFromValue(int value) {
        return switch (value) {
            case 0 -> AutoConnectType.INVALID;
            case 1 -> AutoConnectType.ROUTE_MESH;
            case 2 -> AutoConnectType.CLIENT_SERVER;
            case 3 -> AutoConnectType.DEALER_MESH;
            case 4 -> AutoConnectType.FANOUT;
            case 5 -> AutoConnectType.SPOT_MESH;
            default -> throw invalid("AutoConnectType", value);
        };
    }

    public static int serviceRoleValue(ServiceRole value) {
        return switch (value) {
            case INVALID -> 0;
            case SPOT -> 2;
            case ROUTER -> 3;
            case DEALER -> 4;
            case PUB -> 5;
            case SUB -> 6;
        };
    }

    public static ServiceRole serviceRoleFromValue(int value) {
        return switch (value) {
            case 0 -> ServiceRole.INVALID;
            case 2 -> ServiceRole.SPOT;
            case 3 -> ServiceRole.ROUTER;
            case 4 -> ServiceRole.DEALER;
            case 5 -> ServiceRole.PUB;
            case 6 -> ServiceRole.SUB;
            default -> throw invalid("ServiceRole", value);
        };
    }

    public static int serviceEventSubjectKindValue(
      ServiceEventSubjectKind value) {
        return switch (value) {
            case NONE -> 0;
            case TOPIC -> 1;
            case PATTERN -> 2;
        };
    }

    public static ServiceEventSubjectKind serviceEventSubjectKindFromValue(
      int value) {
        return switch (value) {
            case 0 -> ServiceEventSubjectKind.NONE;
            case 1 -> ServiceEventSubjectKind.TOPIC;
            case 2 -> ServiceEventSubjectKind.PATTERN;
            default -> throw invalid("ServiceEventSubjectKind", value);
        };
    }

    public static int serviceKindValue(ServiceKind value) {
        return switch (value) {
            case DISCOVERY -> 1;
            case SPOT_SUB -> 3;
            case SPOT_PUB -> 4;
            case SOCKET -> 5;
        };
    }

    public static ServiceKind serviceKindFromValue(int value) {
        return switch (value) {
            case 1 -> ServiceKind.DISCOVERY;
            case 3 -> ServiceKind.SPOT_SUB;
            case 4 -> ServiceKind.SPOT_PUB;
            case 5 -> ServiceKind.SOCKET;
            default -> throw invalid("ServiceKind", value);
        };
    }

    public static RegistryState registryStateFromValue(int value) {
        return switch (value) {
            case 1 -> RegistryState.IDLE;
            case 2 -> RegistryState.ACTIVE;
            case 3 -> RegistryState.DEGRADED;
            case 4 -> RegistryState.ERROR;
            default -> throw invalid("RegistryState", value);
        };
    }

    public static int registryStateValue(RegistryState value) {
        return switch (value) {
            case IDLE -> 1;
            case ACTIVE -> 2;
            case DEGRADED -> 3;
            case ERROR -> 4;
        };
    }

    public static int topologySourceValue(TopologySource value) {
        return switch (value) {
            case MANUAL -> 1;
            case DISCOVERY -> 2;
            case REGISTRY -> 3;
        };
    }

    public static TopologySource topologySourceFromValue(int value) {
        return switch (value) {
            case 1 -> TopologySource.MANUAL;
            case 2 -> TopologySource.DISCOVERY;
            case 3 -> TopologySource.REGISTRY;
            default -> throw invalid("TopologySource", value);
        };
    }

    public static int topologyStateValue(TopologyState value) {
        return switch (value) {
            case DISCOVERED -> 1;
            case CONNECTING -> 2;
            case READY -> 3;
            case LOST -> 4;
            case ERROR -> 5;
            case STOPPED -> 6;
        };
    }

    public static TopologyState topologyStateFromValue(int value) {
        return switch (value) {
            case 1 -> TopologyState.DISCOVERED;
            case 2 -> TopologyState.CONNECTING;
            case 3 -> TopologyState.READY;
            case 4 -> TopologyState.LOST;
            case 5 -> TopologyState.ERROR;
            case 6 -> TopologyState.STOPPED;
            default -> throw invalid("TopologyState", value);
        };
    }

    public static int actorAdmissionResultValue(ActorAdmissionResult value) {
        return switch (value) {
            case ACCEPT -> 1;
            case REJECT -> 2;
        };
    }

    public static ActorCreateStatus actorCreateStatusFromValue(int value) {
        return switch (value) {
            case 1 -> ActorCreateStatus.CREATED;
            case 2 -> ActorCreateStatus.EXISTING;
            default -> throw invalid("ActorCreateStatus", value);
        };
    }

    public static int actorCreateStatusValue(ActorCreateStatus value) {
        return switch (value) {
            case CREATED -> 1;
            case EXISTING -> 2;
        };
    }

    public static int spotNodeModeValue(SpotNodeMode value) {
        return switch (value) {
            case PUBSUB -> 1;
            case ROUTED -> 2;
            case ALL -> 3;
        };
    }

    public static int spotNodeSocketOwnerValue(SpotNodeSocketOwner value) {
        return switch (value) {
            case ANY -> 0;
            case NODE -> 1;
            case SPOT -> 2;
        };
    }

    public static SpotNodeSocketOwner spotNodeSocketOwnerFromValue(int value) {
        return switch (value) {
            case 0 -> SpotNodeSocketOwner.ANY;
            case 1 -> SpotNodeSocketOwner.NODE;
            case 2 -> SpotNodeSocketOwner.SPOT;
            default -> SpotNodeSocketOwner.ANY;
        };
    }

    public static SpotNodeState spotNodeStateFromValue(int value) {
        return switch (value) {
            case 1 -> SpotNodeState.IDLE;
            case 2 -> SpotNodeState.CONNECTING;
            case 3 -> SpotNodeState.PARTIAL_READY;
            case 4 -> SpotNodeState.READY;
            case 5 -> SpotNodeState.ERROR;
            default -> throw invalid("SpotNodeState", value);
        };
    }

    public static int spotNodeStateValue(SpotNodeState value) {
        return switch (value) {
            case IDLE -> 1;
            case CONNECTING -> 2;
            case PARTIAL_READY -> 3;
            case READY -> 4;
            case ERROR -> 5;
        };
    }

    public static int spotPeerSourceValue(SpotPeerSource value) {
        return switch (value) {
            case MANUAL -> 1;
            case DISCOVERY -> 2;
            case MIXED -> 3;
        };
    }

    public static SpotPeerSource spotPeerSourceFromValue(int value) {
        return switch (value) {
            case 1 -> SpotPeerSource.MANUAL;
            case 2 -> SpotPeerSource.DISCOVERY;
            case 3 -> SpotPeerSource.MIXED;
            default -> throw invalid("SpotPeerSource", value);
        };
    }

    public static int spotPeerStateValue(SpotPeerState value) {
        return switch (value) {
            case CONFIGURED -> 1;
            case CONNECTING -> 2;
            case CONNECTED -> 3;
        };
    }

    public static SpotPeerState spotPeerStateFromValue(int value) {
        return switch (value) {
            case 1 -> SpotPeerState.CONFIGURED;
            case 2 -> SpotPeerState.CONNECTING;
            case 3 -> SpotPeerState.CONNECTED;
            default -> throw invalid("SpotPeerState", value);
        };
    }

    public static int spotRoleValue(SpotRole value) {
        return switch (value) {
            case PUB -> 1;
            case SUB -> 2;
        };
    }

    public static SpotRole spotRoleFromValue(int value) {
        return switch (value) {
            case 1 -> SpotRole.PUB;
            case 2 -> SpotRole.SUB;
            default -> throw invalid("SpotRole", value);
        };
    }

    private static IllegalArgumentException invalid(String type, long value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
