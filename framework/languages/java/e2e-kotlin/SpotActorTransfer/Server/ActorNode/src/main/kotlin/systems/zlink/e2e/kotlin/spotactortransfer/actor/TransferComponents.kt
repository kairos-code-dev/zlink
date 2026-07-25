package systems.zlink.e2e.kotlin.spotactortransfer.actor

import java.time.Duration
import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.spotactortransfer.shared.Contracts
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory
import systems.zlink.framework.kotlin.ZLinkSuspendingActorTransferAdapter
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorSendHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingSession
import systems.zlink.framework.kotlin.addHandler
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionMessageContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler
import systems.zlink.framework.actors.ZLinkActorJoinResult

class TransferActor(
    private val id: String,
    private val actorContext: ZLinkActorContext,
) : ZLinkActor {
    var actorType: String = Contracts.STATEFUL
    var stateVersion: Int = 0

    override fun actorId(): String = id
    override fun context(): ZLinkActorContext = actorContext
}

class TransferActorFactory(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingActorFactory() {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor {
        if (evidence.nodeRid == "actor-b" && actorId.startsWith("actor-no-adapter-")) {
            evidence.add("transfer", actorId, "transfer_in_empty_default", "actor-factory")
        }
        return TransferActor(actorId, context)
    }
}

class TransferActorAdapter(
    private val evidence: EvidenceStore,
    private val gates: GateStore,
) : ZLinkSuspendingActorTransferAdapter<TransferActor>() {
    override suspend fun transferOutSuspending(
        actor: TransferActor,
    ): ZLinkMessage {
        if (actor.actorType == Contracts.FAIL_OUT) {
            evidence.add("ST-C3", actor.actorId(), "transfer_out_failed", actor.stateVersion.toString())
            error("injected transfer out failure")
        }
        if (actor.actorType == Contracts.EMPTY_STATE) {
            evidence.add("transfer", actor.actorId(), "transfer_out_empty", "custom-adapter")
            return ZLinkMessage.empty()
        }
        evidence.add("transfer", actor.actorId(), "transfer_out", actor.stateVersion.toString())
        if (actor.actorId().startsWith("actor-source-down-before-commit-")) {
            evidence.add("ST-C1", actor.actorId(), "before_commit_gate", actor.stateVersion.toString())
            gates.waitFor(actor.actorId()).await()
        }
        return ZLinkMessage.of(Contracts.TransferState(actor.actorId(), actor.stateVersion, actor.actorType))
    }

    override suspend fun transferInSuspending(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): TransferActor {
        if (state.isEmpty) {
            evidence.add("transfer", actorId, "transfer_in_empty", "custom-adapter")
            return TransferActor(actorId, context).also { it.actorType = Contracts.EMPTY_STATE }
        }
        val transferred = state.decode(Contracts.TransferState::class.java)
        if (actorId.startsWith("actor-fail-transfer-in-")) {
            evidence.add("ST-C3", actorId, "transfer_in_failed", transferred.stateVersion().toString())
            error("injected transfer in failure")
        }
        return TransferActor(actorId, context).also { actor ->
            actor.actorType = transferred.actorType()
            actor.stateVersion = transferred.stateVersion()
            evidence.add("transfer", actorId, "transfer_in", actor.stateVersion.toString())
        }
    }
}

class TransferEntrySpot(
    private val entryContext: ZLinkEntrySpotContext,
    private val evidence: EvidenceStore,
    private val domainState: DomainStateStore,
) : ZLinkSuspendingEntrySpot<TransferActor>() {
    override fun context(): ZLinkEntrySpotContext = entryContext

    override fun configure() {
        entryContext.handlers().addHandler<JoinTargetHandler>()
        entryContext.handlers().addHandler<EntryProbeHandler>()
        entryContext.handlers().addHandler<EntryBoundPushHandler>()
    }

    override suspend fun onCreateActorSuspending(
        actor: TransferActor,
        createRequest: ZLinkMessage,
    ) {
        if (!createRequest.isEmpty) {
            val request = createRequest.decode(Contracts.ActorCreateReq::class.java)
            actor.actorType = request.actorType()
            actor.stateVersion = request.stateVersion()
            if (actor.actorType == Contracts.EMPTY_STATE) {
                domainState.save(actor.actorId(), actor.stateVersion)
            }
        }
        evidence.add("create", actor.actorId(), "create", "${actor.actorType}:${actor.stateVersion}")
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse {
        evidence.add("local", actorId, "admission", "actor-id-only")
        return ZLinkSpotActorJoinResponse.accept(request)
    }

    override suspend fun onJoinedActorSuspending(actor: TransferActor) {
        evidence.add("local", actor.actorId(), "entry_joined", actor.stateVersion.toString())
    }

    override suspend fun onLeaveActorSuspending(actor: TransferActor) {
        if (actor.actorType == Contracts.NO_ADAPTER) {
            evidence.add("transfer", actor.actorId(), "transfer_out_empty_default", "no-adapter")
        }
        if (actor.actorType == Contracts.FAIL_LEAVE) {
            evidence.add("ST-C3", actor.actorId(), "leave_failed", actor.stateVersion.toString())
            error("injected source leave failure")
        }
        evidence.add("transfer", actor.actorId(), "leave", actor.stateVersion.toString())
    }
}

class TransferUserSpot(
    private val spotContext: ZLinkSpotContext,
    private val evidence: EvidenceStore,
    private val gates: GateStore,
    private val domainState: DomainStateStore,
) : ZLinkSuspendingSpot<TransferActor>() {
    private val scenarios = ConcurrentHashMap<String, String>()
    private var mode = "accept"

    override fun context(): ZLinkSpotContext = spotContext

    override fun configure() {
        spotContext.handlers().addHandler<UserJoinTargetHandler>()
        spotContext.handlers().addHandler<ProbeHandler>()
        spotContext.handlers().addHandler<BoundPushHandler>()
        spotContext.handlers().addHandler<StragglerSendHandler>()
    }

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse {
        if (!request.isEmpty) {
            mode = request.decode(Contracts.CreateSpotReq::class.java).mode()
                ?.takeIf(String::isNotBlank) ?: "accept"
        }
        evidence.add("create_spot", spotContext.spotRid().toString(), "spot_created", mode)
        return ZLinkSpotCreateResponse.accept()
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse {
        val join = request.decode(Contracts.JoinTargetReq::class.java)
        scenarios[actorId] = join.scenario()
        evidence.add(
            join.scenario(),
            actorId,
            "admission",
            "spot=${spotContext.spotRid()};mode=$mode;input=actor-id-only",
        )
        val reject = mode == "reject" || join.expectedMode() == "reject"
        val response = Contracts.JoinTargetRes(
            join.scenario(), actorId, !reject, "", spotContext.spotRid().toString(), 0,
        )
        return if (reject) ZLinkSpotActorJoinResponse.reject(response)
        else ZLinkSpotActorJoinResponse.accept(response)
    }

    override suspend fun onJoinedActorSuspending(actor: TransferActor) {
        val scenario = scenarios[actor.actorId()] ?: "transfer"
        if (mode == "delay-joined") {
            evidence.add(scenario, actor.actorId(), "joined_wait", spotContext.spotRid().toString())
            gates.waitFor(spotContext.spotRid().toString()).await()
            evidence.add(scenario, actor.actorId(), "joined_released", spotContext.spotRid().toString())
        }
        if (mode == "fail-joined") {
            evidence.add(scenario, actor.actorId(), "joined_failed", spotContext.spotRid().toString())
            error("injected joined failure")
        }
        evidence.add("transfer", actor.actorId(), "joined", "${spotContext.spotRid()}:${actor.stateVersion}")
        if (actor.actorType == Contracts.EMPTY_STATE) {
            actor.stateVersion = domainState.load(actor.actorId())
            evidence.add("transfer", actor.actorId(), "domain_state_loaded", actor.stateVersion.toString())
        }
    }

    override suspend fun onLeaveActorSuspending(actor: TransferActor) {
        evidence.add("transfer", actor.actorId(), "target_leave", spotContext.spotRid().toString())
    }
}

class JoinTargetHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingEntrySpotActorRequestHandler<
    TransferEntrySpot,
    TransferActor,
    Contracts.JoinTargetReq,
    Contracts.JoinTargetRes
> {
    override suspend fun handle(
        entrySpot: TransferEntrySpot,
        actor: TransferActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.JoinTargetReq,
    ): Contracts.JoinTargetRes {
        return joinTarget(actor, request, evidence)
    }
}

class UserJoinTargetHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingSpotActorRequestHandler<
    TransferUserSpot,
    TransferActor,
    Contracts.JoinTargetReq,
    Contracts.JoinTargetRes
> {
    override suspend fun handle(
        spot: TransferUserSpot,
        actor: TransferActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.JoinTargetReq,
    ): Contracts.JoinTargetRes = joinTarget(actor, request, evidence)
}

private suspend fun joinTarget(
    actor: TransferActor,
    request: Contracts.JoinTargetReq,
    evidence: EvidenceStore,
): Contracts.JoinTargetRes {
    val joined = actor.context()
        .joinSpot(RoutingId.from(request.targetSpotRid()), request)
        .timeout(Duration.ofSeconds(10))
        .submit(Contracts.JoinTargetRes::class.java).await()
    evidence.add(request.scenario(), actor.actorId(), "commit_ack", request.targetSpotRid())
    return Contracts.JoinTargetRes(
        request.scenario(), actor.actorId(), joined is ZLinkActorJoinResult.Accepted<*>, evidence.nodeRid,
        request.targetSpotRid(), actor.stateVersion,
    )
}

class EntryProbeHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingEntrySpotActorRequestHandler<
    TransferEntrySpot,
    TransferActor,
    Contracts.ProbeReq,
    Contracts.ProbeRes
> {
    override suspend fun handle(
        entrySpot: TransferEntrySpot,
        actor: TransferActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.ProbeReq,
    ): Contracts.ProbeRes {
        evidence.add(request.scenario(), actor.actorId(), "entry_packet_handler", request.marker())
        return Contracts.ProbeRes(
            request.scenario(), actor.actorId(), entrySpot.context().spotRid().toString(),
            evidence.nodeRid, actor.stateVersion, request.marker(),
        )
    }
}

class ProbeHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingSpotActorRequestHandler<
    TransferUserSpot,
    TransferActor,
    Contracts.ProbeReq,
    Contracts.ProbeRes
> {
    override suspend fun handle(
        spot: TransferUserSpot,
        actor: TransferActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.ProbeReq,
    ): Contracts.ProbeRes {
        evidence.add(request.scenario(), actor.actorId(), "packet_handler", request.marker())
        return Contracts.ProbeRes(
            request.scenario(), actor.actorId(), spot.context().spotRid().toString(),
            evidence.nodeRid, actor.stateVersion, request.marker(),
        )
    }
}

class StragglerSendHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingSpotActorSendHandler<
    TransferUserSpot,
    TransferActor,
    Contracts.StragglerSendReq
> {
    override suspend fun handle(
        spot: TransferUserSpot,
        actor: TransferActor,
        context: ZLinkSpotActorSendContext,
        message: Contracts.StragglerSendReq,
    ) {
        evidence.add(message.scenario(), actor.actorId(), "straggler_send", message.marker())
    }
}

class TransferSession(
    private val sessionContext: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
    private val evidence: EvidenceStore,
) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = sessionContext

    override suspend fun onConnectedSuspending() {
        evidence.add("session", sessionContext.sessionId(), "connected", "")
    }

    override suspend fun onDisconnectedSuspending() {
        evidence.add("session", sessionContext.sessionId(), "disconnected", "")
    }

    override suspend fun onErrorSuspending(error: ZLinkStreamError) {
        evidence.add("session", sessionContext.sessionId(), "error", error.toString())
    }

    override suspend fun onDispatchSuspending(
        dispatch: ZLinkSessionMessageContext,
        payload: ZLinkMessage,
    ) {
        if (handlers.tryHandle(sessionContext, dispatch, payload).await()) return
        val actor = sessionContext.actors().bound().singleOrNull()
            ?: sessionContext.actors().find(dispatch.metadata()["actor-id"])
                .orElseThrow { IllegalStateException("actor is not bound") }
        actor.relay(dispatch, payload).await()
    }
}

class BindSessionHandler(
    private val actors: systems.zlink.framework.actors.ZLinkActorManager,
    private val evidence: EvidenceStore,
) : ZLinkSuspendingTypedSessionPacketHandler<ZLinkSessionContext, Contracts.BindSessionReq> {
    override fun packetName(): String = "BindSessionReq"
    override fun messageType(): Class<Contracts.BindSessionReq> = Contracts.BindSessionReq::class.java

    override suspend fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionMessageContext,
        request: Contracts.BindSessionReq,
    ) {
        val actor = actors.find(request.actorId()).await()
            .orElseThrow { IllegalStateException("actor was not found: ${request.actorId()}") }
        val bound = context.actors().bind(actor).await()
        evidence.add(request.scenario(), request.actorId(), "session_bound", context.sessionId())
        context.client().reply(
            Contracts.BindSessionRes(
                request.scenario(), request.actorId(), bound.ref().nodeRid().toString(),
                bound.ref().generation(),
            ),
        ).submit()
    }
}

class EntryBoundPushHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingEntrySpotActorRequestHandler<
    TransferEntrySpot,
    TransferActor,
    Contracts.BoundPushReq,
    Contracts.BoundPushRes
> {
    override suspend fun handle(
        entrySpot: TransferEntrySpot,
        actor: TransferActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.BoundPushReq,
    ): Contracts.BoundPushRes = push(actor, entrySpot.context().spotRid().toString(), request, evidence)
}

class BoundPushHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingSpotActorRequestHandler<
    TransferUserSpot,
    TransferActor,
    Contracts.BoundPushReq,
    Contracts.BoundPushRes
> {
    override suspend fun handle(
        spot: TransferUserSpot,
        actor: TransferActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.BoundPushReq,
    ): Contracts.BoundPushRes = push(actor, spot.context().spotRid().toString(), request, evidence)
}

private fun push(
    actor: TransferActor,
    spotRid: String,
    request: Contracts.BoundPushReq,
    evidence: EvidenceStore,
): Contracts.BoundPushRes {
    actor.context().boundSession()
        .send(
            Contracts.BoundPushNotify(
                request.scenario(), actor.actorId(), spotRid, evidence.nodeRid,
                request.marker(), actor.stateVersion,
            ),
        )
        .submit()
    evidence.add(request.scenario(), actor.actorId(), "bound_push", request.marker())
    return Contracts.BoundPushRes(
        request.scenario(), actor.actorId(), spotRid, evidence.nodeRid,
        request.marker(), actor.stateVersion,
    )
}
