package systems.zlink.e2e.kotlin.spotservice.play.spots

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.handlers.*
import java.time.Duration
import systems.zlink.framework.kotlin.addHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse

class UserSpot(
    private val context: ZLinkSpotContext,
    private val evidence: ScenarioState
) : ZLinkSuspendingSpot<ScenarioActor>() {
    private val pendingProfiles = mutableMapOf<String, Contracts.ActorProfile>()
    private var state = ""
    private var workerDone = true

    override fun context(): ZLinkSpotContext = context

    override fun configure() {
        context.handlers().addHandler<StateRequestHandler>()
        context.handlers().addHandler<StateCommandHandler>()
        context.handlers().addHandler<StageProbeHandler>()
        context.handlers().addHandler<StageTimerStartHandler>()
        context.handlers().addHandler<SlowRequestHandler>()
        context.handlers().addHandler<OutboundRequestHandler>()
        context.handlers().addHandler<SpotToSpotCommandHandler>()
        context.handlers().addHandler<OutboundCommandHandler>()
        context.handlers().addHandler<SpotEventHandler>()
        context.handlers().addHandler<UserActorEchoHandler>()
        context.handlers().addHandler<UserActorLeaveHandler>()
    }

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse {
        evidence.record("SpotCreated", context.spotRid().toString(), if (request.isEmpty) "" else "request")
        context.addTimer("state-timer", Duration.ofSeconds(2), StateTimerHandler::class.java, null)
        return ZLinkSpotCreateResponse.accept()
    }

    override suspend fun onInitializeSuspending() {
        evidence.record("SpotInitialized", context.spotRid().toString(), "")
    }

    override suspend fun onClosingSuspending() {
        evidence.record("SpotClosing", context.spotRid().toString(), state)
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse {
        val join = request.decode(Contracts.ActorJoinReq::class.java)
        pendingProfiles[actorId] = join.profile
        evidence.record(
            "ActorUserJoinRequested",
            context.spotRid().toString(),
            actorId + "/" + join.profile.displayName + "/" + join.tags.joinToString(",")
        )
        return ZLinkSpotActorJoinResponse.accept(
            Contracts.ActorJoinRes(
                actorId,
                context.spotRid().toString(),
                evidence.nodeRid(),
                join.profile.displayName,
                join.profile.level,
                join.tags
            )
        )
    }

    override suspend fun onJoinedActorSuspending(actor: ScenarioActor) {
        actor.applyProfile(
            pendingProfiles.remove(actor.actorId())
                ?: error("joined actor does not have a pending admission")
        )
        evidence.record("ActorUserJoined", context.spotRid().toString(), actor.actorId() + "#" + actor.nextSequence())
    }

    override suspend fun onLeaveActorSuspending(actor: ScenarioActor) {
        evidence.record("ActorUserLeft", context.spotRid().toString(), actor.actorId())
    }

    override suspend fun onDisconnectActorSuspending(actor: ScenarioActor) {
        evidence.record("ActorUserDisconnected", context.spotRid().toString(), actor.actorId())
    }

    fun apply(op: String): String {
        state = if (state.isBlank()) op else "$state,$op"
        evidence.record("StateReq", context.spotRid().toString(), state)
        if (op == "worker-follow-up" && !workerDone) {
            evidence.record("WorkerFollowUpBeforeComplete", context.spotRid().toString(), state)
        }
        return state
    }

    fun startWorker(op: String): String {
        workerDone = false
        evidence.record("WorkerStarted", context.spotRid().toString(), op)
        context.runCpuWorker {
            val delayMillis = if (op == "worker-start-long") 5000L else 1500L
            Thread.sleep(delayMillis)
            "$op-done"
        }.submit().whenComplete { value, error ->
            if (error != null) {
                evidence.record("WorkerFailed", context.spotRid().toString(), error.javaClass.simpleName)
                return@whenComplete
            }
            workerDone = true
            state = if (state.isBlank()) value else "$state,$value"
            evidence.record("WorkerCompleted", context.spotRid().toString(), value)
        }
        return state
    }

    fun command(value: String) {
        evidence.record("StateMsg", context.spotRid().toString(), value)
    }

    fun record(marker: String, value: String) {
        evidence.record(marker, context.spotRid().toString(), value)
    }

    fun spotRid(): String = context.spotRid().toString()

    fun nodeRid(): String = evidence.nodeRid()

    fun timerTick(deliveryIndex: Long) {
        evidence.record("SpotTimer", context.spotRid().toString(), deliveryIndex.toString())
    }
}
