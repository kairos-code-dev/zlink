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
builder.Services.AddTransient<TransferActorAdapter>();
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
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeStateless)
        .AddStatelessActorTransfer<TransferActor>(SpotActorTransferNames.ActorTypeStateless)
        .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeNoAdapter)
        .AddSpotFactory<TransferUserSpot>();
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

    internal sealed class TransferActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new TransferActor(actorId, context));
        }
    }

    internal sealed class TransferActorAdapter(EvidenceStore evidence)
        : IZLinkActorTransferAdapter<TransferActor>
    {
        public ValueTask<ZLinkMessage> TransferOutAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("transfer", actor.ActorId, "transfer_out", actor.StateVersion.ToString());
            return ValueTask.FromResult(ZLinkMessage.From(new TransferStateDto(actor.ActorId, actor.StateVersion)));
        }

        public ValueTask<TransferActor> TransferInAsync(
            string actorId,
            IZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var dto = state.Decode<TransferStateDto>();
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
        EvidenceStore evidence) : IZLinkEntrySpot<TransferActor>
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
            }

            evidence.Add("create", actor.ActorId, "create", $"{actor.ActorType}:{actor.StateVersion}");
            return ValueTask.CompletedTask;
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            ZLinkActorJoinAdmission admission,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("local", admission.ActorId, "admission", admission.ActorType.Name);
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
            evidence.Add("transfer", actor.ActorId, "leave", actor.StateVersion.ToString());
            return ValueTask.CompletedTask;
        }
    }

    internal sealed class TransferUserSpot(
        IZLinkSpotContext context,
        EvidenceStore evidence) : IZLinkSpot<TransferActor>
    {
        private string _mode = "accept";

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
            ZLinkActorJoinAdmission admission,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var join = request.Decode<JoinTargetReq>();
            evidence.Add(join.Scenario, admission.ActorId, "admission", $"spot={Context.SpotRid}|mode={_mode}");
            if (string.Equals(_mode, "reject", StringComparison.Ordinal)
                || string.Equals(join.ExpectedMode, "reject", StringComparison.Ordinal))
                return ValueTask.FromResult(ZLinkSpotActorJoinResult.Reject(new JoinTargetRes(
                    join.Scenario,
                    admission.ActorId,
                    false,
                    admission.SourceNodeRid.ToString(),
                    Context.SpotRid.ToString(),
                    0)));
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(new JoinTargetRes(
                join.Scenario,
                admission.ActorId,
                true,
                admission.SourceNodeRid.ToString(),
                Context.SpotRid.ToString(),
                0)));
        }

        public ValueTask OnJoinedActorAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            evidence.Add("transfer", actor.ActorId, "joined", $"{Context.SpotRid}:{actor.StateVersion}");
            if (actor.ActorType == SpotActorTransferNames.ActorTypeStateless)
                evidence.Add("transfer", actor.ActorId, "domain_state_loaded", actor.ActorId);
            return ValueTask.CompletedTask;
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
}
