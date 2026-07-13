package systems.zlink.e2e.kotlin.spotservice.multinode

import systems.zlink.e2e.kotlin.spotservice.Contracts
import kotlinx.coroutines.future.await
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler
import systems.zlink.framework.kotlin.addHandler
import systems.zlink.framework.handlers.ZLinkSpotRequest
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.framework.spots.SpotHandleResolver

class MultiNodeSpotA(
    private val context: ZLinkSpotContext,
    private val evidence: MultiNodeEvidenceStore,
    private val handles: SpotHandleResolver,
) : ZLinkSuspendingSpot<MultiNodeActor>() {
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

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse =
        onCreateMultiNodeSpot(context, evidence, handles, request)

    override suspend fun onInitializeSuspending() {
        evidence.add("multi-spot-initialize|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${context.spotRid()}")
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse {
        return ZLinkSpotActorJoinResponse.accept()
    }

    override suspend fun onJoinedActorSuspending(actor: MultiNodeActor) {
        evidence.add("spot-actor-joined|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${context.spotRid()}|actor=${actor.actorId()}")
    }

    override suspend fun onLeaveActorSuspending(actor: MultiNodeActor) {
    }
}

class MultiNodeSpotB(
    private val context: ZLinkSpotContext,
    private val evidence: MultiNodeEvidenceStore,
    private val handles: SpotHandleResolver,
) : ZLinkSuspendingSpot<MultiNodeActor>() {
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

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse =
        onCreateMultiNodeSpot(context, evidence, handles, request)

    override suspend fun onInitializeSuspending() {
        evidence.add("multi-spot-initialize|node=${Contracts.MULTI_SPOT_NODE_B}|spot=${context.spotRid()}")
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse {
        return ZLinkSpotActorJoinResponse.accept()
    }

    override suspend fun onJoinedActorSuspending(actor: MultiNodeActor) {
        evidence.add("spot-actor-joined|node=${Contracts.MULTI_SPOT_NODE_B}|spot=${context.spotRid()}|actor=${actor.actorId()}")
    }

    override suspend fun onLeaveActorSuspending(actor: MultiNodeActor) {
    }
}

private suspend fun onCreateMultiNodeSpot(
    context: ZLinkSpotContext,
    evidence: MultiNodeEvidenceStore,
    handles: SpotHandleResolver,
    request: ZLinkMessage
): ZLinkSpotCreateResponse {
    evidence.add("spot-created|node=${context.nodeRid()}|spot=${context.spotRid()}")
    if (request.isEmpty) {
        return ZLinkSpotCreateResponse.accept()
    }
    val command = request.decode(Contracts.SpotOnlyMeshReq::class.java)
    val target = handles.resolveSpotHandle(systems.zlink.contracts.core.RoutingId.from(command.targetSpotRid))
        .await().orElseThrow { IllegalStateException("spot handle was not found: ${command.targetSpotRid}") }
    val reply = context.outbound()
        .requestToSpot(target, Contracts.MultiNodeStateReq("add", 7))
        .timeout(java.time.Duration.ofSeconds(5))
        .submit(Contracts.MultiNodeStateRes::class.java).await()
    context.outbound()
        .sendToSpot(target, Contracts.StateMsg("sm-f6-send-${command.marker}"))
        .submit()
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
    @ZLinkSpotRequest
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
) : ZLinkSuspendingSpotPacketHandler<MultiNodeSpotA, Contracts.StateMsg> {
    override suspend fun handle(
        spot: MultiNodeSpotA,
        command: Contracts.StateMsg
    ) {
        evidence.add("spot-state-command|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${spot.context().spotRid()}|marker=${command.value}")
    }
}

class MultiNodeStateBHandler(
    private val evidence: MultiNodeEvidenceStore
) {
    @ZLinkSpotRequest
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
) : ZLinkSuspendingSpotPacketHandler<MultiNodeSpotB, Contracts.StateMsg> {
    override suspend fun handle(
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

class MultiNodeActorFactory : ZLinkSuspendingActorFactory() {
    override suspend fun createActor(
        actorId: String,
        context: ZLinkActorContext
    ): ZLinkActor = MultiNodeActor(actorId, context)
}

class MultiNodeEntrySpot(
    private val context: ZLinkEntrySpotContext
) : ZLinkSuspendingEntrySpot<MultiNodeActor>() {
    override suspend fun onCreateActorSuspending(actor: MultiNodeActor, createRequest: ZLinkMessage) {
    }
    override suspend fun onActorJoinSuspending(actorId: String, request: ZLinkMessage) = ZLinkSpotActorJoinResponse.accept()
    override suspend fun onJoinedActorSuspending(actor: MultiNodeActor) {
    }

    override suspend fun onLeaveActorSuspending(actor: MultiNodeActor) {
    }
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
        context.handlers().addHandler<MultiNodeSpotOnlyJoinHandler>()
    }
}

class MultiNodeSpotOnlyJoinHandler : ZLinkSuspendingEntrySpotActorRequestHandler<MultiNodeEntrySpot, MultiNodeActor, Contracts.SpotOnlyJoinReq, Contracts.SpotOnlyJoinRes> {
    @ZLinkSpotActorRequest(packetName = "SpotOnlyJoinReq")
    override suspend fun handle(
        spot: MultiNodeEntrySpot,
        actor: MultiNodeActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.SpotOnlyJoinReq,
    ): Contracts.SpotOnlyJoinRes {
        actor.context()
            .joinSpot(systems.zlink.contracts.core.RoutingId.from(request.targetSpotRid), request)
            .submit().await()
        return Contracts.SpotOnlyJoinRes(true, actor.actorId())
    }
}
