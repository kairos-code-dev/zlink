package systems.zlink.e2e.spotactortransfer.actor;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.e2e.spotactortransfer.shared.Contracts;

public final class TransferComponents {
    private TransferComponents() {
    }

    public static final class TransferActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;
        private String actorType = Contracts.STATEFUL;
        private int stateVersion;

        public TransferActor(String actorId, ZLinkActorContext context) {
            this.actorId = actorId;
            this.context = context;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public ZLinkActorContext context() {
            return context;
        }

        public String actorType() {
            return actorType;
        }

        public void setActorType(String actorType) {
            this.actorType = actorType;
        }

        public int stateVersion() {
            return stateVersion;
        }

        public void setStateVersion(int stateVersion) {
            this.stateVersion = stateVersion;
        }
    }

    public static final class TransferActorFactory implements ZLinkActorFactory {
        private final EvidenceStore evidence;

        public TransferActorFactory(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<ZLinkActor> create(String actorId, ZLinkActorContext context) {
            if ("actor-b".equals(evidence.nodeRid()) && actorId.startsWith("actor-no-adapter-")) {
                evidence.add("transfer", actorId, "transfer_in_empty_default", "actor-factory");
            }
            return CompletableFuture.completedFuture(new TransferActor(actorId, context));
        }
    }

    public static final class TransferActorAdapter
        implements ZLinkActorTransferAdapter<TransferActor> {
        private final EvidenceStore evidence;
        private final GateStore transferGates;

        public TransferActorAdapter(EvidenceStore evidence, GateStore transferGates) {
            this.evidence = evidence;
            this.transferGates = transferGates;
        }

        @Override
        public CompletionStage<ZLinkMessage> transferOut(TransferActor actor) {
            if (Contracts.FAIL_OUT.equals(actor.actorType())) {
                evidence.add("ST-C3", actor.actorId(), "transfer_out_failed",
                    Integer.toString(actor.stateVersion()));
                return CompletableFuture.failedFuture(
                    new IllegalStateException("injected transfer out failure"));
            }
            if (Contracts.EMPTY_STATE.equals(actor.actorType())) {
                evidence.add("transfer", actor.actorId(), "transfer_out_empty", "custom-adapter");
                return CompletableFuture.completedFuture(ZLinkMessage.empty());
            }
            evidence.add("transfer", actor.actorId(), "transfer_out",
                Integer.toString(actor.stateVersion()));
            if (actor.actorId().startsWith("actor-source-down-before-commit-")) {
                evidence.add("ST-C1", actor.actorId(), "before_commit_gate",
                    Integer.toString(actor.stateVersion()));
                return transferGates.waitFor(actor.actorId()).thenApply(ignored ->
                    transferState(actor));
            }
            return CompletableFuture.completedFuture(transferState(actor));
        }

        private static ZLinkMessage transferState(TransferActor actor) {
            return ZLinkMessage.of(new Contracts.TransferState(
                actor.actorId(), actor.stateVersion(), actor.actorType()));
        }

        @Override
        public CompletionStage<TransferActor> transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state) {
            if (state.isEmpty()) {
                evidence.add("transfer", actorId, "transfer_in_empty", "custom-adapter");
                TransferActor actor = new TransferActor(actorId, context);
                actor.setActorType(Contracts.EMPTY_STATE);
                return CompletableFuture.completedFuture(actor);
            }
            Contracts.TransferState transferred = state.decode(Contracts.TransferState.class);
            if (actorId.startsWith("actor-fail-transfer-in-")) {
                evidence.add("ST-C3", actorId, "transfer_in_failed",
                    Integer.toString(transferred.stateVersion()));
                return CompletableFuture.failedFuture(
                    new IllegalStateException("injected transfer in failure"));
            }
            TransferActor actor = new TransferActor(actorId, context);
            actor.setActorType(transferred.actorType());
            actor.setStateVersion(transferred.stateVersion());
            evidence.add("transfer", actorId, "transfer_in",
                Integer.toString(actor.stateVersion()));
            return CompletableFuture.completedFuture(actor);
        }
    }

    public static final class TransferEntrySpot implements ZLinkEntrySpot<TransferActor> {
        private final ZLinkEntrySpotContext context;
        private final EvidenceStore evidence;
        private final DomainStateStore domainState;

        public TransferEntrySpot(
            ZLinkEntrySpotContext context,
            EvidenceStore evidence,
            DomainStateStore domainState) {
            this.context = context;
            this.evidence = evidence;
            this.domainState = domainState;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addHandler(JoinTargetHandler.class);
            context.handlers().addHandler(EntryProbeHandler.class);
            context.handlers().addHandler(EntryBoundPushHandler.class);
        }

        @Override
        public CompletionStage<Void> onCreateActor(
            TransferActor actor,
            ZLinkMessage createRequest) {
            if (!createRequest.isEmpty()) {
                Contracts.ActorCreateReq request = createRequest.decode(Contracts.ActorCreateReq.class);
                actor.setActorType(request.actorType());
                actor.setStateVersion(request.stateVersion());
                if (Contracts.EMPTY_STATE.equals(request.actorType())) {
                    domainState.save(actor.actorId(), actor.stateVersion());
                }
            }
            evidence.add("create", actor.actorId(), "create",
                actor.actorType() + ":" + actor.stateVersion());
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            evidence.add("local", actorId, "admission", "actor-id-only");
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept(request));
        }

        @Override
        public CompletionStage<Void> onJoinedActor(TransferActor actor) {
            evidence.add("local", actor.actorId(), "entry_joined",
                Integer.toString(actor.stateVersion()));
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(TransferActor actor) {
            if (Contracts.NO_ADAPTER.equals(actor.actorType())) {
                evidence.add("transfer", actor.actorId(), "transfer_out_empty_default", "no-adapter");
            }
            if (Contracts.FAIL_LEAVE.equals(actor.actorType())) {
                evidence.add("ST-C3", actor.actorId(), "leave_failed",
                    Integer.toString(actor.stateVersion()));
                return CompletableFuture.failedFuture(
                    new IllegalStateException("injected source leave failure"));
            }
            evidence.add("transfer", actor.actorId(), "leave",
                Integer.toString(actor.stateVersion()));
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class TransferUserSpot implements ZLinkSpot<TransferActor> {
        private final ZLinkSpotContext context;
        private final EvidenceStore evidence;
        private final GateStore joinedGates;
        private final DomainStateStore domainState;
        private final ConcurrentHashMap<String, String> scenarios = new ConcurrentHashMap<>();
        private String mode = "accept";

        public TransferUserSpot(
            ZLinkSpotContext context,
            EvidenceStore evidence,
            GateStore joinedGates,
            DomainStateStore domainState) {
            this.context = context;
            this.evidence = evidence;
            this.joinedGates = joinedGates;
            this.domainState = domainState;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public void configure() {
            context.handlers().addHandler(UserJoinTargetHandler.class);
            context.handlers().addHandler(ProbeHandler.class);
            context.handlers().addHandler(BoundPushHandler.class);
            context.handlers().addHandler(StragglerSendHandler.class);
        }

        @Override
        public CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
            if (!request.isEmpty()) {
                String requested = request.decode(Contracts.CreateSpotReq.class).mode();
                mode = requested == null || requested.isBlank() ? "accept" : requested;
            }
            evidence.add("create_spot", context.spotRid().toString(), "spot_created", mode);
            return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            Contracts.JoinTargetReq join = request.decode(Contracts.JoinTargetReq.class);
            scenarios.put(actorId, join.scenario());
            evidence.add(join.scenario(), actorId, "admission",
                "spot=" + context.spotRid() + ";mode=" + mode + ";input=actor-id-only");
            boolean reject = "reject".equals(mode) || "reject".equals(join.expectedMode());
            Contracts.JoinTargetRes response = new Contracts.JoinTargetRes(
                join.scenario(), actorId, !reject, "", context.spotRid().toString(), 0);
            return CompletableFuture.completedFuture(reject
                ? ZLinkSpotActorJoinResponse.reject(response)
                : ZLinkSpotActorJoinResponse.accept(response));
        }

        @Override
        public CompletionStage<Void> onJoinedActor(TransferActor actor) {
            String scenario = scenarios.getOrDefault(actor.actorId(), "transfer");
            if ("delay-joined".equals(mode)) {
                evidence.add(scenario, actor.actorId(), "joined_wait", context.spotRid().toString());
                return joinedGates.waitFor(context.spotRid().toString()).thenCompose(ignored -> {
                    evidence.add(scenario, actor.actorId(), "joined_released", context.spotRid().toString());
                    return completeJoined(actor);
                });
            }
            return completeJoined(actor);
        }

        private CompletionStage<Void> completeJoined(TransferActor actor) {
            if ("fail-joined".equals(mode)) {
                String scenario = scenarios.getOrDefault(actor.actorId(), "transfer");
                evidence.add(scenario, actor.actorId(), "joined_failed", context.spotRid().toString());
                return CompletableFuture.failedFuture(
                    new IllegalStateException("injected joined failure"));
            }
            evidence.add("transfer", actor.actorId(), "joined",
                context.spotRid() + ":" + actor.stateVersion());
            if (Contracts.EMPTY_STATE.equals(actor.actorType())) {
                actor.setStateVersion(domainState.load(actor.actorId()));
                evidence.add("transfer", actor.actorId(), "domain_state_loaded",
                    Integer.toString(actor.stateVersion()));
            }
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(TransferActor actor) {
            evidence.add("transfer", actor.actorId(), "target_leave", context.spotRid().toString());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class JoinTargetHandler implements ZLinkEntrySpotActorRequestHandler<
        TransferEntrySpot,
        TransferActor,
        Contracts.JoinTargetReq,
        Contracts.JoinTargetRes> {
        private final EvidenceStore evidence;

        public JoinTargetHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Contracts.JoinTargetRes> handle(
            TransferEntrySpot entrySpot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.JoinTargetReq request) {
            return joinTarget(actor, request, evidence);
        }
    }

    public static final class UserJoinTargetHandler implements ZLinkSpotActorRequestHandler<
        TransferUserSpot,
        TransferActor,
        Contracts.JoinTargetReq,
        Contracts.JoinTargetRes> {
        private final EvidenceStore evidence;

        public UserJoinTargetHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Contracts.JoinTargetRes> handle(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.JoinTargetReq request) {
            return joinTarget(actor, request, evidence);
        }
    }

    private static CompletionStage<Contracts.JoinTargetRes> joinTarget(
        TransferActor actor,
        Contracts.JoinTargetReq request,
        EvidenceStore evidence) {
        return actor.context()
            .joinSpot(RoutingId.from(request.targetSpotRid()), request)
            .timeout(Duration.ofSeconds(10))
            .submit(Contracts.JoinTargetRes.class)
            .thenApply(joined -> {
                evidence.add(request.scenario(), actor.actorId(), "commit_ack", request.targetSpotRid());
                return new Contracts.JoinTargetRes(
                    request.scenario(), actor.actorId(),
                    joined instanceof ZLinkActorJoinResult.Accepted<?>, evidence.nodeRid(),
                    request.targetSpotRid(), actor.stateVersion());
            });
    }

    public static final class EntryProbeHandler implements ZLinkEntrySpotActorRequestHandler<
        TransferEntrySpot,
        TransferActor,
        Contracts.ProbeReq,
        Contracts.ProbeRes> {
        private final EvidenceStore evidence;

        public EntryProbeHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Contracts.ProbeRes> handle(
            TransferEntrySpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ProbeReq request) {
            evidence.add(request.scenario(), actor.actorId(), "entry_packet_handler", request.marker());
            return CompletableFuture.completedFuture(new Contracts.ProbeRes(
                request.scenario(), actor.actorId(), spot.context().spotRid().toString(),
                evidence.nodeRid(), actor.stateVersion(), request.marker()));
        }
    }

    public static final class ProbeHandler implements ZLinkSpotActorRequestHandler<
        TransferUserSpot,
        TransferActor,
        Contracts.ProbeReq,
        Contracts.ProbeRes> {
        private final EvidenceStore evidence;

        public ProbeHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Contracts.ProbeRes> handle(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ProbeReq request) {
            evidence.add(request.scenario(), actor.actorId(), "packet_handler", request.marker());
            return CompletableFuture.completedFuture(new Contracts.ProbeRes(
                request.scenario(), actor.actorId(), spot.context().spotRid().toString(),
                evidence.nodeRid(), actor.stateVersion(), request.marker()));
        }
    }

    public static final class StragglerSendHandler implements systems.zlink.framework.spots.ZLinkSpotActorSendHandler<
        TransferUserSpot,
        TransferActor,
        Contracts.StragglerSendReq> {
        private final EvidenceStore evidence;

        public StragglerSendHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Void> handle(
            TransferUserSpot spot,
            TransferActor actor,
            systems.zlink.framework.spots.ZLinkSpotActorSendContext context,
            Contracts.StragglerSendReq request) {
            evidence.add(request.scenario(), actor.actorId(), "straggler_send", request.marker());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class TransferSession implements ZLinkSession {
        private final ZLinkSessionContext context;
        private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
        private final EvidenceStore evidence;

        public TransferSession(
            ZLinkSessionContext context,
            ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
            EvidenceStore evidence) {
            this.context = context;
            this.handlers = handlers;
            this.evidence = evidence;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            evidence.add("session", context.sessionId(), "connected", "");
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            evidence.add("session", context.sessionId(), "disconnected", "");
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            evidence.add("session", context.sessionId(), "error", error.toString());
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
            return handlers.tryHandle(context, dispatch, payload).thenCompose(handled -> {
                if (handled) {
                    return CompletableFuture.completedFuture(null);
                }
                ZLinkSessionActor actor = context.actors().bound().size() == 1
                    ? context.actors().bound().get(0)
                    : context.actors().find(dispatch.metadata().get("actor-id"))
                        .orElseThrow(() -> new IllegalStateException("actor is not bound"));
                return actor.relay(dispatch, payload);
            });
        }
    }

    public static final class BindSessionHandler implements ZLinkTypedSessionPacketHandler<
        ZLinkSessionContext,
        Contracts.BindSessionReq> {
        private final systems.zlink.framework.actors.ZLinkActorManager actors;
        private final EvidenceStore evidence;

        public BindSessionHandler(
            systems.zlink.framework.actors.ZLinkActorManager actors,
            EvidenceStore evidence) {
            this.actors = actors;
            this.evidence = evidence;
        }

        @Override
        public Class<Contracts.BindSessionReq> messageType() {
            return Contracts.BindSessionReq.class;
        }

        @Override
        public CompletionStage<Void> handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            Contracts.BindSessionReq request) {
            return actors.find(request.actorId()).thenCompose(found -> {
                var actor = found.orElseThrow(
                    () -> new IllegalStateException("actor was not found: " + request.actorId()));
                return context.actors().bind(actor);
            }).thenApply(bound -> {
                evidence.add(request.scenario(), request.actorId(), "session_bound", context.sessionId());
                context.client().reply(new Contracts.BindSessionRes(
                    request.scenario(), request.actorId(), bound.ref().nodeRid().toString(),
                    bound.ref().generation())).submit();
                return null;
            });
        }
    }

    public static final class EntryBoundPushHandler implements ZLinkEntrySpotActorRequestHandler<
        TransferEntrySpot,
        TransferActor,
        Contracts.BoundPushReq,
        Contracts.BoundPushRes> {
        private final EvidenceStore evidence;

        public EntryBoundPushHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Contracts.BoundPushRes> handle(
            TransferEntrySpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.BoundPushReq request) {
            return push(actor, spot.context().spotRid().toString(), request, evidence);
        }
    }

    public static final class BoundPushHandler implements ZLinkSpotActorRequestHandler<
        TransferUserSpot,
        TransferActor,
        Contracts.BoundPushReq,
        Contracts.BoundPushRes> {
        private final EvidenceStore evidence;

        public BoundPushHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Contracts.BoundPushRes> handle(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.BoundPushReq request) {
            return push(actor, spot.context().spotRid().toString(), request, evidence);
        }
    }

    private static CompletionStage<Contracts.BoundPushRes> push(
        TransferActor actor,
        String spotRid,
        Contracts.BoundPushReq request,
        EvidenceStore evidence) {
        Contracts.BoundPushNotify notify = new Contracts.BoundPushNotify(
            request.scenario(), actor.actorId(), spotRid, evidence.nodeRid(),
            request.marker(), actor.stateVersion());
        actor.context().boundSession()
            .send(notify)
            .submit();
        evidence.add(request.scenario(), actor.actorId(), "bound_push", request.marker());
        return CompletableFuture.completedFuture(new Contracts.BoundPushRes(
            request.scenario(), actor.actorId(), spotRid, evidence.nodeRid(),
            request.marker(), actor.stateVersion()));
    }
}
