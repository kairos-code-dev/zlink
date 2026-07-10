package systems.zlink.e2e.kotlin.spotservice.multinode

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory
import systems.zlink.framework.kotlin.addHandler
import systems.zlink.framework.handlers.ZLinkSpotRequest
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.locations.SpotRef
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.framework.spots.ZLinkSpotPacketHandler

class MultiNodeSpotA(
    private val context: ZLinkSpotContext,
    private val evidence: MultiNodeEvidenceStore
) : ZLinkSpot<MultiNodeActor> {
    private var value = 0

    override fun context(): ZLinkSpotContext =
        context

    override fun configure() {
        context.handlers().addHandler<MultiNodeStateAHandler>()
        context.handlers().addHandler<MultiNodeStateCommandAHandler>()
    }

    fun add(delta: Int): Int {
        value += delta
        return value
    }

    override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse =
        onCreateMultiNodeSpot(context, evidence, request)

    override fun onInitialize() {
        evidence.add("multi-spot-initialize|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${context.spotRid()}")
    }

    override fun onActorJoin(
        actorId: String,
        request: ZLinkMessage,
        cancellationToken: CancellationToken
    ): ZLinkSpotActorJoinResponse {
        return ZLinkSpotActorJoinResponse.accept()
    }

    override fun onJoinedActor(actor: MultiNodeActor, cancellationToken: CancellationToken) {
        evidence.add("spot-actor-joined|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${context.spotRid()}|actor=${actor.actorId()}")
    }

    override fun onLeaveActor(actor: MultiNodeActor, cancellationToken: CancellationToken) {
    }
}

class MultiNodeSpotB(
    private val context: ZLinkSpotContext,
    private val evidence: MultiNodeEvidenceStore
) : ZLinkSpot<MultiNodeActor> {
    private var value = 0

    override fun context(): ZLinkSpotContext =
        context

    override fun configure() {
        context.handlers().addHandler<MultiNodeStateBHandler>()
        context.handlers().addHandler<MultiNodeStateCommandBHandler>()
    }

    fun add(delta: Int): Int {
        value += delta
        return value
    }

    override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse =
        onCreateMultiNodeSpot(context, evidence, request)

    override fun onInitialize() {
        evidence.add("multi-spot-initialize|node=${Contracts.MULTI_SPOT_NODE_B}|spot=${context.spotRid()}")
    }

    override fun onActorJoin(
        actorId: String,
        request: ZLinkMessage,
        cancellationToken: CancellationToken
    ): ZLinkSpotActorJoinResponse {
        return ZLinkSpotActorJoinResponse.accept()
    }

    override fun onJoinedActor(actor: MultiNodeActor, cancellationToken: CancellationToken) {
        evidence.add("spot-actor-joined|node=${Contracts.MULTI_SPOT_NODE_B}|spot=${context.spotRid()}|actor=${actor.actorId()}")
    }

    override fun onLeaveActor(actor: MultiNodeActor, cancellationToken: CancellationToken) {
    }
}

private fun onCreateMultiNodeSpot(
    context: ZLinkSpotContext,
    evidence: MultiNodeEvidenceStore,
    request: ZLinkMessage
): ZLinkSpotCreateResponse {
    evidence.add("spot-created|node=${context.nodeRid()}|spot=${context.spotRid()}")
    if (request.isEmpty) {
        return ZLinkSpotCreateResponse.accept()
    }
    val command = request.decode(Contracts.SpotOnlyMeshReq::class.java)
    val target = SpotRef(
        Contracts.SPOT_MESH,
        systems.zlink.contracts.core.RoutingId.from(Contracts.MULTI_SPOT_NODE_B),
        systems.zlink.contracts.core.RoutingId.from(command.targetSpotRid)
    )
    val reply = context.outbound()
        .requestToSpot(target, Contracts.MultiNodeStateReq("add", 7))
        .packetName("StateReq")
        .timeout(java.time.Duration.ofSeconds(5))
        .await(Contracts.MultiNodeStateRes::class.java)
    context.outbound()
        .sendToSpot(target, Contracts.StateMsg("sm-f6-send-${command.marker}"))
        .packetName("StateMsg")
        .await()
    evidence.add(
        "spot-only-request|node=${context.nodeRid()}|source=${context.spotRid()}" +
            "|target=${command.targetSpotRid}|value=${reply.value}|marker=${command.marker}"
    )
    return ZLinkSpotCreateResponse.accept(
        Contracts.SpotOnlyMeshRes(
            context.spotRid().toString(),
            command.targetSpotRid,
            reply.value
        )
    )
}

class MultiNodeStateAHandler(
    private val evidence: MultiNodeEvidenceStore
) {
    @ZLinkSpotRequest(packetName = "StateReq")
    fun handle(
        spot: MultiNodeSpotA,
        request: Contracts.MultiNodeStateReq
    ): Contracts.MultiNodeStateRes {
        val delta = if (request.operation == "add") request.delta else 0
        val value = spot.add(delta)
        evidence.add("multi-state-request|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${spot.context().spotRid()}|value=$value")
        return Contracts.MultiNodeStateRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            value
        )
    }
}

class MultiNodeStateCommandAHandler(
    private val evidence: MultiNodeEvidenceStore
) : ZLinkSpotPacketHandler<MultiNodeSpotA, Contracts.StateMsg> {
    override fun handle(
        spot: MultiNodeSpotA,
        command: Contracts.StateMsg
    ) {
        evidence.add("spot-state-command|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${spot.context().spotRid()}|marker=${command.value}")
    }
}

class MultiNodeStateBHandler(
    private val evidence: MultiNodeEvidenceStore
) {
    @ZLinkSpotRequest(packetName = "StateReq")
    fun handle(
        spot: MultiNodeSpotB,
        request: Contracts.MultiNodeStateReq
    ): Contracts.MultiNodeStateRes {
        val delta = if (request.operation == "add") request.delta else 0
        val value = spot.add(delta)
        evidence.add("multi-state-request|node=${Contracts.MULTI_SPOT_NODE_B}|spot=${spot.context().spotRid()}|value=$value")
        return Contracts.MultiNodeStateRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            value
        )
    }
}

class MultiNodeStateCommandBHandler(
    private val evidence: MultiNodeEvidenceStore
) : ZLinkSpotPacketHandler<MultiNodeSpotB, Contracts.StateMsg> {
    override fun handle(
        spot: MultiNodeSpotB,
        command: Contracts.StateMsg
    ) {
        evidence.add("spot-state-command|node=${Contracts.MULTI_SPOT_NODE_B}|spot=${spot.context().spotRid()}|marker=${command.value}")
    }
}

class MultiNodeActor(
    private val actorId: String,
    private val context: ZLinkActorContext
) : ZLinkActor {
    override fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context
}

class MultiNodeActorFactory : ZLinkActorFactory {
    override fun create(
        actorId: String,
        context: ZLinkActorContext
    ): ZLinkActor = MultiNodeActor(actorId, context)
}

class MultiNodeEntrySpot(
    private val context: ZLinkEntrySpotContext
) : ZLinkEntrySpot<MultiNodeActor> {
    override fun onJoinedActor(actor: MultiNodeActor, cancellationToken: CancellationToken) {
    }

    override fun onLeaveActor(actor: MultiNodeActor, cancellationToken: CancellationToken) {
    }
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
        context.handlers().addHandler<MultiNodeSpotOnlyJoinHandler>()
    }
}

class MultiNodeSpotOnlyJoinHandler {
    @ZLinkSpotActorRequest(packetName = "SpotOnlyJoinReq")
    fun handle(
        spot: MultiNodeEntrySpot,
        actor: MultiNodeActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.SpotOnlyJoinReq,
        cancellationToken: CancellationToken
    ): Contracts.SpotOnlyJoinRes {
        actor.context()
            .joinSpot(systems.zlink.contracts.core.RoutingId.from(request.targetSpotRid), request)
            .await()
        return Contracts.SpotOnlyJoinRes(true, actor.actorId())
    }
}
