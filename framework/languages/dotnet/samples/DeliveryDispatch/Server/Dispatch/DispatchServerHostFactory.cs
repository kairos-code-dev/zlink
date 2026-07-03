using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.Dispatch;

public static class DispatchServerHostFactory
{
    public static WebApplication Build(SampleTopology topology)
    {
        var builder = WebApplication.CreateBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("DELIVERYDISPATCH_LOG_DIR"),
            "dispatch");
        builder.WebHost.UseUrls(topology.DispatchHttpUrl);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<EvidenceStore>();
        builder.Services.AddSingleton<DispatchWorkQueue>();
        builder.Services.AddSingleton<CourierOfferPort>();
        builder.Services.AddSingleton<DeliveryStatusPublisher>();
        builder.Services.AddHostedService<DispatchWorker>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("dispatch"))
                .TraceLabel("dispatch");
            options.AddHandlersFromAssemblyOf(typeof(DispatchServerHostFactory));
            options.AddClientServerChannel(SampleNames.DispatchChannel)
                .EnableServer(topology.DispatchChannelEndpoint)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-channel"))
                .AddHandlerGroup(SampleNames.DispatchChannel);
            options.AddRouteMesh(SampleNames.CourierActorNodeRouteChannel)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-courier-client"));
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-tracking-client"));
        });

        var app = builder.Build();
        app.MapGet("/health", async (
            IZLinkLocationRuntimeQuery locations,
            CancellationToken cancellationToken) =>
        {
            var node1Ready = await HasReadyCourierRouteAsync(
                locations,
                topology.CourierActorNode1Rid,
                cancellationToken);
            var node2Ready = await HasReadyCourierRouteAsync(
                locations,
                topology.CourierActorNode2Rid,
                cancellationToken);
            return node1Ready && node2Ready
                ? Results.Ok(new { ready = true, role = "dispatch" })
                : Results.StatusCode(StatusCodes.Status503ServiceUnavailable);
        });
        app.MapPost("/deliveries", (
            CreateDeliveryReq request,
            Zlink.Framework.Contracts.Channels.IZLinkChannelClient channels,
            ILoggerFactory loggerFactory,
            CancellationToken cancellationToken) =>
        {
            var assign = new AssignDeliveryMsg(
                request.DeliveryId,
                request.CustomerId,
                request.PickupAddress,
                request.DropoffAddress);
            channels.SendToChannel(SampleNames.DispatchChannel, assign)
                .PacketName(nameof(AssignDeliveryMsg))
                .Submit(cancellationToken);
            loggerFactory.CreateLogger("DeliveryDispatch.Server.Dispatch")
                .LogInformation("deliverydispatch api: created delivery={DeliveryId}", request.DeliveryId);
            return Results.Ok(new CreateDeliveryRes(request.DeliveryId));
        });
        app.MapPost("/self-check/assert", (
            ServerAssertionReq request,
            EvidenceStore evidence) =>
        {
            var success = evidence.HasSequence(
                request.SuccessfulDeliveryId,
                DeliveryStatus.Assigned,
                DeliveryStatus.Accepted,
                DeliveryStatus.PickedUp,
                DeliveryStatus.Delivered);
            var reassigned = evidence.HasSequence(
                request.ReassignedDeliveryId,
                DeliveryStatus.Assigned,
                DeliveryStatus.Reassigned,
                DeliveryStatus.Accepted,
                DeliveryStatus.Delivered);
            return Results.Ok(new ServerAssertionRes(success && reassigned, evidence.ReadLines()));
        });

        return app;
    }

    private static async ValueTask<bool> HasReadyCourierRouteAsync(
        IZLinkLocationRuntimeQuery locations,
        RoutingId nodeRid,
        CancellationToken cancellationToken)
    {
        try
        {
            var routes = await locations.ListTopologyAsync(
                new ZLinkLocationTopologyFilter(
                    Kind: ZLinkLocationKind.Peer,
                    MeshName: SampleNames.CourierActorNodeRouteChannel,
                    Role: ZLinkLocationRole.Router,
                    NodeRid: nodeRid,
                    State: ZLinkLocationTopologyState.Ready),
                cancellationToken: cancellationToken);
            return routes.Items.Count > 0;
        }
        catch
        {
            return false;
        }
    }
}
