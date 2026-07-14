using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Locations;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.Dispatch;

public static class DispatchServerHostFactory
{
    public static WebApplication Build(SampleConfiguration configuration)
    {
        var topology = configuration.Topology;
        var builder = WebApplication.CreateBuilder();
        SampleLogging.Configure(
            builder.Logging,
            configuration.Role.LogDir,
            "dispatch");
        builder.WebHost.UseUrls(topology.DispatchHttpUrl);
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<EvidenceStore>();
        builder.Services.AddSingleton<DispatchWorkQueue>();
        builder.Services.AddSingleton<DeliveryOfferStore>();
        builder.Services.AddSingleton<CourierSelectionPolicy>();
        builder.Services.AddSingleton<CourierOfferPort>();
        builder.Services.AddSingleton<DeliveryStatusPublisher>();
        builder.Services.AddSingleton<DispatchWorker>();
        builder.Services.AddHostedService<DispatchQueuePump>();
        builder.Services.AddHostedService<OfferDeadlineSweeper>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(configuration.FlowLogPath)
                .TraceLabel("dispatch");
            options.AddHandlersFromAssemblyOf(typeof(DispatchServerHostFactory));
            options.AddClientServerChannel(SampleNames.DispatchChannel)
                .EnableServer(topology.DispatchChannelEndpoint)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-channel"))
                .AddHandlerGroup(SampleNames.DispatchChannel);
            options.AddSpotMesh(SampleNames.CourierActorDiscovery)
                .EnableRouter(topology.DispatchSpotRouterEndpoint)
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-courier-client"));
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-tracking-client"));
        });

        var app = builder.Build();
        app.MapGet("/health", async (
            IZLinkLocationReadiness readiness,
            CancellationToken cancellationToken) =>
        {
            var courierNode1Ready = await readiness.IsPeerReadyAsync(
                SampleNames.CourierActorDiscovery,
                ZLinkLocationRole.Spot,
                topology.CourierActorNode1Rid,
                cancellationToken);
            var courierNode2Ready = await readiness.IsPeerReadyAsync(
                SampleNames.CourierActorDiscovery,
                ZLinkLocationRole.Spot,
                topology.CourierActorNode2Rid,
                cancellationToken);

            return courierNode1Ready && courierNode2Ready
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
                DeliveryStatus.PickedUp,
                DeliveryStatus.Delivered);
            return Results.Ok(new ServerAssertionRes(success && reassigned, evidence.ReadLines()));
        });

        return app;
    }
}
