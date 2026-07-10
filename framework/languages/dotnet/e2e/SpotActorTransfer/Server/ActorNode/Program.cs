using System.Text.Json;
using SpotActorTransfer.ActorNode;
using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Locations.Redis;

var options = ServerOptions.Parse(args, "actor-node");
Directory.CreateDirectory(options.LogDir);

var builder = WebApplication.CreateBuilder(args);
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(console =>
{
    console.SingleLine = true;
    console.TimestampFormat = "HH:mm:ss.fff ";
});
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
builder.Services.AddSingleton(new DomainStateStore(options.LogDir));
builder.Services.AddSingleton<JoinedGateStore>();
builder.Services.AddSingleton<TransferGateStore>();
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(options.RedisEndpoint)
        .SetKeyPrefix(options.RedisKeyPrefix)));
    var locations = framework.ConfigureLocations();
    locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
    locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
    locations.PollingInterval = TimeSpan.FromMilliseconds(500);

    framework.AddHandlersFromAssemblyOf(typeof(Program));
    framework.AddSpotMesh(SpotActorTransferNames.Mesh)
        .EnableRouter(options.RouterEndpoint)
        .SetRoutingId(RoutingId.From(options.Rid))
        .SetEntrySpotRoutingId(RoutingId.From(SpotActorTransferNames.EntrySpotRid))
        .AddEntrySpot<TransferEntrySpot>()
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeStateful)
        .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeStateful)
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeEmptyState)
        .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeEmptyState)
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeNoAdapter)
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeFailLeave)
        .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeFailLeave)
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeFailTransferOut)
        .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeFailTransferOut)
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeFailTransferIn)
        .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeFailTransferIn)
        .AddSpotFactory<TransferUserSpot>();
    framework.AddStreamNode($"{SpotActorTransferNames.Mesh}-stream-{options.Rid}")
        .Bind(options.StreamEndpoint)
        .RegisterSession<TransferSession>();
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ok", options.Rid }));
app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
app.MapPost("/evidence/wait", async (
    EvidenceWaitReq request,
    EvidenceStore evidence,
    CancellationToken cancellationToken) =>
{
    var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
    var snapshot = await evidence.WaitUntilAsync(
        entries => request.ContainsAll.All(expected =>
            entries.Any(entry => EvidenceText(entry).Contains(expected, StringComparison.Ordinal))),
        timeout,
        cancellationToken);
    return Results.Ok(snapshot);
});
app.MapPost("/joined-gates/{spotRid}/release", (
    string spotRid,
    JoinedGateStore gates) =>
{
    return Results.Ok(new GateReleaseRes(spotRid, gates.Release(spotRid)));
});
app.MapPost("/transfer-gates/{actorId}/release", (
    string actorId,
    TransferGateStore gates) =>
{
    return Results.Ok(new GateReleaseRes(actorId, gates.Release(actorId)));
});
app.MapPost("/spots", async (
    CreateSpotReq request,
    IZLinkSpotManager spots,
    CancellationToken cancellationToken) =>
{
    var result = await spots.GetOrCreateAsync<TransferUserSpot, CreateSpotReq>(
        RoutingId.From(request.SpotRid),
        request,
        cancellationToken);
    return Results.Ok(new CreateSpotRes(
        result.SpotRid.ToString(),
        options.Rid,
        result.State.ToString()));
});
app.MapPost("/actors", async (
    ActorCreateReq request,
    IZLinkActorManager actors,
    CancellationToken cancellationToken) =>
{
    var actor = await actors.GetOrCreateAsync(
        request.ActorId,
        request.ActorType,
        request,
        cancellationToken);
    return Results.Ok(new ActorCreateRes(
        actor.ActorId,
        request.ActorType,
        actor.NodeRid.ToString(),
        checked((long)actor.Generation)));
});
app.MapGet("/actors/{actorId}/ref", async (
    string actorId,
    IZLinkActorManager actors,
    CancellationToken cancellationToken) =>
{
    var actor = await actors.FindAsync(actorId, cancellationToken)
                ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");
    return Results.Ok(new ActorRefSnapshotRes(
        actor.ActorId,
        actor.NodeRid.ToString(),
        checked((long)actor.Generation)));
});
app.MapPost("/actors/{actorId}/join", async (
    string actorId,
    JoinTargetReq request,
    IZLinkActorManager actors,
    IZLinkActorClient actorClient,
    EvidenceStore evidence,
    CancellationToken cancellationToken) =>
{
    var actor = await actors.FindAsync(actorId, cancellationToken)
                ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");
    try
    {
        var result = await actorClient.RequestToActor(actor, request)
            .PacketName(nameof(JoinTargetReq))
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<JoinTargetRes>(cancellationToken);
        evidence.Add(request.Scenario, actorId, result.Accepted ? "success_reply" : "reject_reply", request.TargetSpotRid);
        return Results.Ok(result);
    }
    catch (ZLinkFrameworkException ex)
    {
        evidence.Add(request.Scenario, actorId, "join_failed", ex.Kind.ToString());
        return Results.Ok(new
        {
            request.Scenario,
            ActorId = actorId,
            Accepted = false,
            ErrorKind = ex.Kind.ToString()
        });
    }
    catch (InvalidOperationException ex)
    {
        evidence.Add(request.Scenario, actorId, "join_failed", ex.Message);
        return Results.Ok(new
        {
            request.Scenario,
            ActorId = actorId,
            Accepted = false,
            ErrorKind = ex.Message
        });
    }
});
app.MapPost("/actors/{actorId}/probe", async (
    string actorId,
    ProbeReq request,
    IZLinkActorManager actors,
    IZLinkActorClient actorClient,
    CancellationToken cancellationToken) =>
{
    var actor = await actors.FindAsync(actorId, cancellationToken)
                ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");
    var response = await actorClient.RequestToActor(actor, request)
        .PacketName(nameof(ProbeReq))
        .Timeout(TimeSpan.FromSeconds(10))
        .Async<ProbeRes>(cancellationToken);
    return Results.Ok(response);
});
app.MapPost("/actors/{actorId}/bound-push", async (
    string actorId,
    BoundPushReq request,
    IZLinkActorManager actors,
    IZLinkActorClient actorClient,
    CancellationToken cancellationToken) =>
{
    var actor = await actors.FindAsync(actorId, cancellationToken)
                ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");
    var response = await actorClient.RequestToActor(actor, request)
        .PacketName(nameof(BoundPushReq))
        .Timeout(TimeSpan.FromSeconds(10))
        .Async<BoundPushRes>(cancellationToken);
    return Results.Ok(response);
});
app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
{
    lifetime.StopApplication();
    return Results.Ok(new { status = "stopping" });
});
await app.RunAsync();

static string EvidenceText(ActorEvidence evidence) =>
    $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}|{evidence.NodeRid}";

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

    internal sealed class TransferSession(
        IZLinkSessionContext context,
        EvidenceStore evidence) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddHandler<BindActorSessionHandler>();
        }

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("session", Context.SessionId, "connected", Context.RoutingId?.ToString() ?? "");
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("session", Context.SessionId, "disconnected", Context.RoutingId?.ToString() ?? "");
            return ValueTask.CompletedTask;
        }

        public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("session", Context.SessionId, "error", error.ToString());
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDispatchAsync(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload,
            CancellationToken cancellationToken)
        {
            if (await Context.Handlers.TryHandleAsync(dispatch, payload, cancellationToken)
                    .ConfigureAwait(false))
                return;

            var actor = Context.Actors.Bound.SingleOrDefault()
                        ?? throw new InvalidOperationException("No actor is bound.");
            await actor.RelayAsync(payload, cancellationToken).ConfigureAwait(false);
        }
    }

    internal sealed class BindActorSessionHandler(
        IZLinkActorManager actors,
        EvidenceStore evidence) : IZLinkSessionPacketHandler<IZLinkSessionContext, BindActorSessionReq>
    {
        public async ValueTask HandleAsync(
            IZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            BindActorSessionReq request,
            CancellationToken cancellationToken)
        {
            _ = dispatch;
            var actorRef = await actors.FindAsync(request.ActorId, cancellationToken);
            ActorRef resolvedActorRef;
            if (actorRef is null)
            {
                if (string.IsNullOrWhiteSpace(request.NodeRid) || request.Generation is null)
                    throw new InvalidOperationException($"Actor '{request.ActorId}' was not found.");
                resolvedActorRef = new ActorRef(
                    RoutingId.From(request.NodeRid),
                    request.ActorId,
                    checked((ulong)request.Generation.Value));
            }
            else
            {
                resolvedActorRef = actorRef.Value;
            }

            await context.Actors.BindAsync(resolvedActorRef, cancellationToken).ConfigureAwait(false);
            evidence.Add(request.Scenario, request.ActorId, "session_bound", context.SessionId);
            context.Client.Reply(new BindActorSessionRes(
                    request.Scenario,
                    resolvedActorRef.ActorId,
                    resolvedActorRef.NodeRid.ToString(),
                    checked((long)resolvedActorRef.Generation)))
                .Submit(cancellationToken);
        }
    }

    internal sealed class JoinTargetHandler(EvidenceStore evidence)
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
            var joined = await actor.Context.JoinSpot(RoutingId.From(request.TargetSpotRid), request)
                .Timeout(TimeSpan.FromSeconds(10))
                .Async<JoinTargetRes>(cancellationToken);
            evidence.Add(request.Scenario, actor.ActorId, "commit_request", request.TargetSpotRid);
            return new JoinTargetRes(
                request.Scenario,
                actor.ActorId,
                joined.Accepted,
                evidence.NodeRid,
                request.TargetSpotRid,
                actor.StateVersion);
        }
    }

    [ZLinkSpotActorRequestHandler(nameof(ProbeReq))]
    internal sealed class ProbeHandler(EvidenceStore evidence)
        : IZLinkSpotActorRequestHandler<TransferUserSpot, TransferActor, ProbeReq, ProbeRes>
    {
        public ValueTask<ProbeRes> HandleAsync(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            ProbeReq request,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add(request.Scenario, actor.ActorId, "packet_handler", request.Marker);
            return ValueTask.FromResult(new ProbeRes(
                request.Scenario,
                actor.ActorId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                actor.StateVersion,
                request.Marker));
        }
    }

    [ZLinkSpotActorRequestHandler(nameof(BoundPushReq))]
    internal sealed class EntryBoundPushHandler(EvidenceStore evidence)
        : IZLinkEntrySpotActorRequestHandler<TransferEntrySpot, TransferActor, BoundPushReq, BoundPushRes>
    {
        public ValueTask<BoundPushRes> HandleAsync(
            TransferEntrySpot entrySpot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            BoundPushReq request,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            actor.Context.BoundSession.Send(new BoundPushNotify(
                    request.Scenario,
                    actor.ActorId,
                    entrySpot.Context.SpotRid.ToString(),
                    entrySpot.Context.NodeRid.ToString(),
                    request.Marker,
                    actor.StateVersion))
                .PacketName(nameof(BoundPushNotify))
                .Submit(cancellationToken);
            evidence.Add(request.Scenario, actor.ActorId, "bound_push", request.Marker);
            return ValueTask.FromResult(new BoundPushRes(
                request.Scenario,
                actor.ActorId,
                entrySpot.Context.SpotRid.ToString(),
                entrySpot.Context.NodeRid.ToString(),
                request.Marker,
                actor.StateVersion));
        }
    }

    [ZLinkSpotActorRequestHandler(nameof(BoundPushReq))]
    internal sealed class BoundPushHandler(EvidenceStore evidence)
        : IZLinkSpotActorRequestHandler<TransferUserSpot, TransferActor, BoundPushReq, BoundPushRes>
    {
        public ValueTask<BoundPushRes> HandleAsync(
            TransferUserSpot spot,
            TransferActor actor,
            ZLinkSpotActorRequestContext context,
            BoundPushReq request,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            actor.Context.BoundSession.Send(new BoundPushNotify(
                    request.Scenario,
                    actor.ActorId,
                    spot.Context.SpotRid.ToString(),
                    spot.Context.NodeRid.ToString(),
                    request.Marker,
                    actor.StateVersion))
                .PacketName(nameof(BoundPushNotify))
                .Submit(cancellationToken);
            evidence.Add(request.Scenario, actor.ActorId, "bound_push", request.Marker);
            return ValueTask.FromResult(new BoundPushRes(
                request.Scenario,
                actor.ActorId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                request.Marker,
                actor.StateVersion));
        }
    }
}
