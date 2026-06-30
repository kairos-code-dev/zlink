package systems.zlink.e2e.kotlin.spotservice.multinode

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.handlers.ZLinkSpotRequest
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext

class MultiNodeSpotA(
    private val context: ZLinkSpotContext,
    private val evidence: MultiNodeEvidenceStore
) : ZLinkSpot<ZLinkActor> {
    private var value = 0

    override fun context(): ZLinkSpotContext =
        context

    override fun configure() {
        context.handlers().addPacket(MultiNodeStateAHandler::class.java)
    }

    override fun onInitialize() {
        evidence.add("multi-spot-initialize|node=${Contracts.MULTI_SPOT_NODE_A}|spot=${context.spotRid()}")
    }

    fun add(delta: Int): Int {
        value += delta
        return value
    }
}

class MultiNodeSpotB(
    private val context: ZLinkSpotContext,
    private val evidence: MultiNodeEvidenceStore
) : ZLinkSpot<ZLinkActor> {
    private var value = 0

    override fun context(): ZLinkSpotContext =
        context

    override fun configure() {
        context.handlers().addPacket(MultiNodeStateBHandler::class.java)
    }

    override fun onInitialize() {
        evidence.add("multi-spot-initialize|node=${Contracts.MULTI_SPOT_NODE_B}|spot=${context.spotRid()}")
    }

    fun add(delta: Int): Int {
        value += delta
        return value
    }
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
