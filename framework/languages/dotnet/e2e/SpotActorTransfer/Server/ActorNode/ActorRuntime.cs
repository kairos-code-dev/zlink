using System.Text.Json;
using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
namespace SpotActorTransfer.ActorNode
{
    internal sealed class TransferActor(
        string actorId,
        IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public string ActorType { get; set; } = SpotActorTransferNames.ActorTypeStateful;

        public int StateVersion { get; set; }

        public IZLinkActorContext Context { get; } = context;
    }

    internal sealed class TransferActorFactory(EvidenceStore evidence) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (evidence.NodeRid == "actor-b"
                && actorId.StartsWith("actor-no-adapter-", StringComparison.Ordinal))
                evidence.Add("transfer", actorId, "transfer_in_empty_default", "actor-factory");
            return ValueTask.FromResult<IZLinkActor>(new TransferActor(actorId, context));
        }
    }

    internal sealed class TransferActorAdapter(
        EvidenceStore evidence,
        TransferGateStore transferGates)
        : IZLinkActorTransferAdapter<TransferActor>
    {
        public async ValueTask<ZLinkMessage> TransferOutAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (actor.ActorType == SpotActorTransferNames.ActorTypeFailTransferOut)
            {
                evidence.Add("ST-C3", actor.ActorId, "transfer_out_failed", actor.StateVersion.ToString());
                throw new InvalidOperationException("injected transfer out failure");
            }

            if (actor.ActorType == SpotActorTransferNames.ActorTypeEmptyState)
            {
                evidence.Add("transfer", actor.ActorId, "transfer_out_empty", "custom-adapter");
                return ZLinkMessage.Empty;
            }

            evidence.Add("transfer", actor.ActorId, "transfer_out", actor.StateVersion.ToString());
            if (actor.ActorId.StartsWith("actor-source-down-before-commit-", StringComparison.Ordinal))
            {
                evidence.Add("ST-C1", actor.ActorId, "before_commit_gate", actor.StateVersion.ToString());
                await transferGates.WaitAsync(actor.ActorId, cancellationToken)
                    .ConfigureAwait(false);
            }

            return ZLinkMessage.From(new TransferStateDto(actor.ActorId, actor.StateVersion));
        }

        public ValueTask<TransferActor> TransferInAsync(
            string actorId,
            IZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (state.IsEmpty)
            {
                evidence.Add("transfer", actorId, "transfer_in_empty", "custom-adapter");
                return ValueTask.FromResult(new TransferActor(actorId, context)
                {
                    ActorType = SpotActorTransferNames.ActorTypeEmptyState
                });
            }

            var dto = state.Decode<TransferStateDto>();
            if (actorId.StartsWith("actor-fail-transfer-in-", StringComparison.Ordinal))
            {
                evidence.Add("ST-C3", actorId, "transfer_in_failed", dto.StateVersion.ToString());
                throw new InvalidOperationException("injected transfer in failure");
            }

            var actor = new TransferActor(actorId, context)
            {
                ActorType = SpotActorTransferNames.ActorTypeStateful,
                StateVersion = dto.StateVersion
            };
            evidence.Add("transfer", actorId, "transfer_in", actor.StateVersion.ToString());
            return ValueTask.FromResult(actor);
        }
    }

    internal sealed class TransferEntrySpot(
        IZLinkEntrySpotContext context,
        EvidenceStore evidence,
        DomainStateStore domainState) : IZLinkEntrySpot<TransferActor>
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public ValueTask OnCreateActorAsync(
            TransferActor actor,
            ZLinkMessage createRequest,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!createRequest.IsEmpty)
            {
                var request = createRequest.Decode<ActorCreateReq>();
                actor.ActorType = request.ActorType;
                actor.StateVersion = request.StateVersion;
                if (actor.ActorType == SpotActorTransferNames.ActorTypeEmptyState)
                    domainState.Save(actor.ActorId, actor.StateVersion);
            }

            evidence.Add("create", actor.ActorId, "create", $"{actor.ActorType}:{actor.StateVersion}");
            return ValueTask.CompletedTask;
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("local", actorId, "admission", "actor-id-only");
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
        }

        public ValueTask OnJoinedActorAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("local", actor.ActorId, "entry_joined", actor.StateVersion.ToString());
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (actor.ActorType == SpotActorTransferNames.ActorTypeNoAdapter)
                evidence.Add("transfer", actor.ActorId, "transfer_out_empty_default", "no-adapter");
            if (actor.ActorType == SpotActorTransferNames.ActorTypeFailLeave)
            {
                evidence.Add("ST-C3", actor.ActorId, "leave_failed", actor.StateVersion.ToString());
                throw new InvalidOperationException("injected source leave failure");
            }

            evidence.Add("transfer", actor.ActorId, "leave", actor.StateVersion.ToString());
            return ValueTask.CompletedTask;
        }
    }

    internal sealed class TransferUserSpot(
        IZLinkSpotContext context,
        EvidenceStore evidence,
        JoinedGateStore joinedGates,
        DomainStateStore domainState) : IZLinkSpot<TransferActor>
    {
        private string _mode = "accept";
        private readonly Dictionary<string, string> _joinScenarios = new(StringComparer.Ordinal);

        public IZLinkSpotContext Context { get; } = context;

        public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!request.IsEmpty) _mode = request.Decode<CreateSpotReq>().Mode;
            evidence.Add("create_spot", Context.SpotRid.ToString(), "spot_created", _mode);
            return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var join = request.Decode<JoinTargetReq>();
            _joinScenarios[actorId] = join.Scenario;
            evidence.Add(join.Scenario, actorId, "admission", $"spot={Context.SpotRid}|mode={_mode}|input=actor-id-only");
            if (string.Equals(_mode, "reject", StringComparison.Ordinal)
                || string.Equals(join.ExpectedMode, "reject", StringComparison.Ordinal))
                return ValueTask.FromResult(ZLinkSpotActorJoinResult.Reject(new JoinTargetRes(
                    join.Scenario,
                    actorId,
                    false,
                    string.Empty,
                    Context.SpotRid.ToString(),
                    0)));
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(new JoinTargetRes(
                join.Scenario,
                actorId,
                true,
                string.Empty,
                Context.SpotRid.ToString(),
                0)));
        }

        public ValueTask OnJoinedActorAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (string.Equals(_mode, "delay-joined", StringComparison.Ordinal))
            {
                var scenario = _joinScenarios.GetValueOrDefault(actor.ActorId, "unknown");
                evidence.Add(scenario, actor.ActorId, "joined_wait", Context.SpotRid.ToString());
                return WaitForJoinedGateAsync(actor, cancellationToken);
            }
            if (string.Equals(_mode, "fail-joined", StringComparison.Ordinal))
            {
                var scenario = _joinScenarios.GetValueOrDefault(actor.ActorId, "unknown");
                evidence.Add(scenario, actor.ActorId, "joined_failed", Context.SpotRid.ToString());
                throw new InvalidOperationException("injected joined failure");
            }

            evidence.Add("transfer", actor.ActorId, "joined", $"{Context.SpotRid}:{actor.StateVersion}");
            if (actor.ActorType == SpotActorTransferNames.ActorTypeEmptyState)
            {
                actor.StateVersion = domainState.Load(actor.ActorId);
                evidence.Add("transfer", actor.ActorId, "domain_state_loaded", actor.ActorId);
            }
            return ValueTask.CompletedTask;
        }

        private async ValueTask WaitForJoinedGateAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            var scenario = _joinScenarios.GetValueOrDefault(actor.ActorId, "unknown");
            await joinedGates.WaitAsync(Context.SpotRid.ToString(), cancellationToken)
                .ConfigureAwait(false);
            evidence.Add(scenario, actor.ActorId, "joined_released", Context.SpotRid.ToString());
            evidence.Add("transfer", actor.ActorId, "joined", $"{Context.SpotRid}:{actor.StateVersion}");
        }

        public ValueTask OnLeaveActorAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("transfer", actor.ActorId, "target_leave", Context.SpotRid.ToString());
            return ValueTask.CompletedTask;
        }
    }

    internal sealed class ActorJoinTargetUseCase(EvidenceStore evidence)
    {
        public async ValueTask<JoinTargetRes> ExecuteAsync(
            TransferActor actor,
            JoinTargetReq request,
            CancellationToken cancellationToken)
        {
            var joined = await actor.Context.JoinSpot(RoutingId.From(request.TargetSpotRid), request)
                .Timeout(TimeSpan.FromSeconds(10))
                .Async<JoinTargetRes>(cancellationToken);
            evidence.Add(request.Scenario, actor.ActorId, "commit_request", request.TargetSpotRid);
            return new JoinTargetRes(
                request.Scenario,
                actor.ActorId,
                joined is ZLinkActorJoinResult<JoinTargetRes>.Accepted,
                evidence.NodeRid,
                request.TargetSpotRid,
                actor.StateVersion);
        }
    }

    internal sealed class JoinTargetHandler(ActorJoinTargetUseCase joinTarget)
        : IZLinkEntrySpotActorRequestHandler<TransferEntrySpot, TransferActor, JoinTargetReq, JoinTargetRes>
    {
        public async ValueTask<JoinTargetRes> HandleAsync(
            TransferEntrySpot entrySpot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            JoinTargetReq request,
            CancellationToken cancellationToken)
        {
            _ = entrySpot;
            _ = context;
            return await joinTarget.ExecuteAsync(actor, request, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    [ZLinkSpotActorRequestHandler(nameof(JoinTargetReq))]
    internal sealed class UserSpotJoinTargetHandler(ActorJoinTargetUseCase joinTarget)
        : IZLinkSpotActorRequestHandler<TransferUserSpot, TransferActor, JoinTargetReq, JoinTargetRes>
    {
        public async ValueTask<JoinTargetRes> HandleAsync(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            JoinTargetReq request,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = context;
            return await joinTarget.ExecuteAsync(actor, request, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    [ZLinkSpotActorRequestHandler(nameof(ProbeReq))]
    internal sealed class ProbeHandler(EvidenceStore evidence)
        : IZLinkSpotActorRequestHandler<TransferUserSpot, TransferActor, ProbeReq, ProbeRes>
    {
        public async ValueTask<ProbeRes> HandleAsync(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            ProbeReq request,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add(request.Scenario, actor.ActorId, "packet_handler", request.Marker);
            if (request.Scenario == "ST-F6" && request.Marker == "late-reply")
            {
                await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
                evidence.Add(request.Scenario, actor.ActorId, "late_reply_created", request.Marker);
            }
            return new ProbeRes(
                request.Scenario,
                actor.ActorId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                actor.StateVersion,
                request.Marker);
        }
    }

    [ZLinkSpotActorSendHandler(nameof(HandoffPacket))]
    internal sealed class HandoffPacketHandler(EvidenceStore evidence)
        : IZLinkSpotActorSendHandler<TransferUserSpot, TransferActor, HandoffPacket>
    {
        public ValueTask HandleAsync(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorSendContext context,
            HandoffPacket message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add(message.Scenario, actor.ActorId, "handoff_packet", message.Marker);
            return ValueTask.CompletedTask;
        }
    }

    [ZLinkSpotActorRequestHandler(nameof(BoundPushReq))]
    internal sealed class EntryBoundPushHandler(EvidenceStore evidence)
        : IZLinkEntrySpotActorRequestHandler<TransferEntrySpot, TransferActor, BoundPushReq, BoundPushRes>
    {
        public async ValueTask<BoundPushRes> HandleAsync(
            TransferEntrySpot entrySpot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            BoundPushReq request,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            await actor.Context.BoundSession.Send(new BoundPushNotify(
                    request.Scenario,
                    actor.ActorId,
                    entrySpot.Context.SpotRid.ToString(),
                    entrySpot.Context.NodeRid.ToString(),
                    request.Marker,
                    actor.StateVersion))
                .SubmitAsync(cancellationToken);
            evidence.Add(request.Scenario, actor.ActorId, "bound_push", request.Marker);
            return new BoundPushRes(
                request.Scenario,
                actor.ActorId,
                entrySpot.Context.SpotRid.ToString(),
                entrySpot.Context.NodeRid.ToString(),
                request.Marker,
                actor.StateVersion);
        }
    }

    [ZLinkSpotActorRequestHandler(nameof(BoundPushReq))]
    internal sealed class BoundPushHandler(EvidenceStore evidence)
        : IZLinkSpotActorRequestHandler<TransferUserSpot, TransferActor, BoundPushReq, BoundPushRes>
    {
        public async ValueTask<BoundPushRes> HandleAsync(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            BoundPushReq request,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            await actor.Context.BoundSession.Send(new BoundPushNotify(
                    request.Scenario,
                    actor.ActorId,
                    spot.Context.SpotRid.ToString(),
                    spot.Context.NodeRid.ToString(),
                    request.Marker,
                    actor.StateVersion))
                .SubmitAsync(cancellationToken);
            evidence.Add(request.Scenario, actor.ActorId, "bound_push", request.Marker);
            return new BoundPushRes(
                request.Scenario,
                actor.ActorId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                request.Marker,
                actor.StateVersion);
        }
    }
}
