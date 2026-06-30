package systems.zlink.e2e.kotlin.spotservice

class Contracts private constructor() {
    companion object {
        const val ROUTE_CHANNEL: String = "spot.service.route"
        const val ROUTE_PACKET: String = "RoutePing"
        const val EGRESS_CHANNEL: String = "spot.service.egress"
        const val INGRESS_CHANNEL: String = "spot.service.ingress"
        const val SPOT_MESH: String = "spot.service.mesh"
        const val SPOT_NODE: String = "play"
        const val MULTI_SPOT_NODE_A: String = "multi-node-a"
        const val MULTI_SPOT_NODE_B: String = "multi-node-b"
        const val MULTI_ROUTE_CHANNEL_A: String = "multi-route-a"
        const val MULTI_ROUTE_CHANNEL_B: String = "multi-route-b"
    }

    @JvmRecord
    data class StateRequest(val op: String)

    @JvmRecord
    data class StateReply(
        val spotRid: String,
        val nodeRid: String,
        val value: String
    )

    @JvmRecord
    data class StateCommand(val value: String)

    @JvmRecord
    data class SlowRequest(val value: String)

    @JvmRecord
    data class OutboundRequest(val value: String)

    @JvmRecord
    data class OutboundReply(
        val spotRid: String,
        val nodeRid: String,
        val channelReply: String
    )

    @JvmRecord
    data class OutboundCommand(val value: String)

    @JvmRecord
    data class MeshEvent(val value: String)

    @JvmRecord
    data class TimerActivity(val value: String)

    @JvmRecord
    data class TimerStatus(
        val spotRid: String,
        val value: String
    )

    @JvmRecord
    data class RoutePing(val value: String)

    @JvmRecord
    data class RoutePong(
        val value: String,
        val nodeRid: String,
        val routeRid: String
    )

    @JvmRecord
    data class MultiNodeCreateSpotRequest(
        val spotRid: String,
        val delta: Int
    )

    @JvmRecord
    data class MultiNodeCreateSpotReply(
        val spotRid: String,
        val nodeRid: String,
        val state: String,
        val value: Int
    )

    @JvmRecord
    data class MultiNodeStateRouteRequest(
        val spotRid: String,
        val delta: Int
    )

    @JvmRecord
    data class MultiNodeStateRequest(
        val operation: String,
        val delta: Int
    )

    @JvmRecord
    data class MultiNodeStateReply(
        val spotRid: String,
        val nodeRid: String,
        val value: Int
    )

    @JvmRecord
    data class EvidenceWaitRequest(
        val containsAll: List<String>,
        val timeoutMilliseconds: Int = 10_000
    )

    @JvmRecord
    data class ActorProfile(
        val displayName: String,
        val level: Int,
        val tags: List<String>
    )

    @JvmRecord
    data class ActorAuthRequest(
        val actorId: String,
        val profile: ActorProfile
    )

    @JvmRecord
    data class ActorAuthReply(
        val actorId: String,
        val nodeRid: String,
        val boundCount: Int,
        val displayName: String,
        val level: Int,
        val tags: List<String>
    )

    @JvmRecord
    data class MultiBindRequest(
        val firstActorId: String,
        val secondActorId: String,
        val profile: ActorProfile
    )

    @JvmRecord
    data class MultiBindReply(
        val boundCount: Int
    )

    @JvmRecord
    data class DestroyActorRequest(
        val actorId: String
    )

    @JvmRecord
    data class DestroyActorReply(
        val actorId: String,
        val destroyed: Boolean
    )

    @JvmRecord
    data class LeaveActorRequest(
        val actorId: String
    )

    @JvmRecord
    data class LeaveActorReply(
        val actorId: String,
        val accepted: Boolean
    )

    @JvmRecord
    data class ActorJoinRequest(
        val spotRid: String,
        val profile: ActorProfile,
        val tags: List<String>
    )

    @JvmRecord
    data class ActorJoinReply(
        val actorId: String,
        val spotRid: String,
        val nodeRid: String,
        val displayName: String,
        val level: Int,
        val tags: List<String>
    )

    @JvmRecord
    data class ActorEchoRequest(
        val value: String,
        val seq: Int,
        val profile: ActorProfile
    )

    @JvmRecord
    data class SlowSessionRequest(
        val value: String,
        val delayMilliseconds: Int
    )

    @JvmRecord
    data class SlowSessionReply(
        val value: String
    )

    @JvmRecord
    data class ActorEchoReply(
        val actorId: String,
        val spotRid: String,
        val nodeRid: String,
        val value: String,
        val requestSeq: Int,
        val handlerSeq: Int,
        val displayName: String,
        val level: Int,
        val tags: List<String>
    )

    @JvmRecord
    data class ActorPush(
        val actorId: String,
        val spotRid: String,
        val value: String,
        val requestSeq: Int,
        val handlerSeq: Int
    )

    @JvmRecord
    data class EvidenceEntry(
        val marker: String,
        val nodeRid: String,
        val spotRid: String,
        val value: String?
    )

    @JvmRecord
    data class EvidenceSnapshot(
        val nodeRid: String,
        val entries: List<EvidenceEntry>
    )
}
