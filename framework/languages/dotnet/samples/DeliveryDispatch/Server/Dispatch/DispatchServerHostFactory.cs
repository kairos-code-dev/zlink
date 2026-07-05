using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
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
            options.AddSpotMesh(SampleNames.CourierActorDiscovery)
                .EnableRouter("inproc://delivery-dispatch-courier-client")
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-courier-client"))
                .ConnectRouter(topology.CourierActorNode1Rid, topology.CourierActorNode1RouterEndpoint)
                .ConnectRouter(topology.CourierActorNode2Rid, topology.CourierActorNode2RouterEndpoint);
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-tracking-client"));
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { ready = true, role = "dispatch" }));
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
}
