package systems.zlink.e2e.kotlin.spotservice.play.spots

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.handlers.*
import java.time.Duration
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.addHandler
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkTimerOptions

class UserSpot(
    private val context: ZLinkSpotContext,
    private val evidence: ScenarioState
) : ZLinkSpot<ScenarioActor> {
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

    override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse {
        evidence.record("SpotCreated", context.spotRid().toString(), if (request.isEmpty) "" else "request")
        context.addTimer("state-timer", Duration.ofSeconds(2), StateTimerHandler::class.java, ZLinkTimerOptions())
        return ZLinkSpotCreateResponse.accept()
    }

    override fun onInitialize() {
        evidence.record("SpotInitialized", context.spotRid().toString(), "")
    }

    override fun onClosing() {
        evidence.record("SpotClosing", context.spotRid().toString(), state)
    }

    override fun onActorJoin(
        actor: ScenarioActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken
    ): ZLinkSpotActorJoinResponse {
        val join = request.decode(Contracts.ActorJoinReq::class.java)
        actor.applyProfile(join.profile)
        evidence.record(
            "ActorUserJoinRequested",
            context.spotRid().toString(),
            actor.actorId() + "/" + join.profile.displayName + "/" + join.tags.joinToString(",")
        )
        return ZLinkSpotActorJoinResponse.accept(
            Contracts.ActorJoinRes(
                actor.actorId(),
                context.spotRid().toString(),
                evidence.nodeRid(),
                join.profile.displayName,
                join.profile.level,
                join.tags
            )
        )
    }

    override fun onJoinedActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
        evidence.record("ActorUserJoined", context.spotRid().toString(), actor.actorId() + "#" + actor.nextSequence())
    }

    override fun onLeaveActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
        evidence.record("ActorUserLeft", context.spotRid().toString(), actor.actorId())
    }

    override fun onDisconnectActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
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
        context.runWorker { token ->
            val delayMillis = if (op == "worker-start-long") 5000L else 1500L
            Thread.sleep(delayMillis)
            "$op-done"
        }.submit(
            { value, token ->
                workerDone = true
                state = if (state.isBlank()) value else "$state,$value"
                evidence.record("WorkerCompleted", context.spotRid().toString(), value)
            },
            { error, token ->
                evidence.record("WorkerFailed", context.spotRid().toString(), error.javaClass.simpleName)
            }
        )
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
