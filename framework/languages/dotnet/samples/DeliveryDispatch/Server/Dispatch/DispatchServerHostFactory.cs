using Microsoft.Extensions.Configuration;

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
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            configuration.Role.LogDir,
            "dispatch");
        builder.WebHost.UseUrls(topology.DispatchHttpUrl);
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
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
            var mesh = options.AddRouteMesh(SampleNames.MeshName)
                .Listen(topology.MeshEndpoint)
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-channel"));
            mesh.ChannelName(SampleNames.DispatchChannel)
                .AddHandlerGroup(SampleNames.DispatchChannel);
            mesh.ChannelName(SampleNames.TrackingRouteChannel).SetWeight(0);
            mesh.ChannelName(SampleNames.MeshName).SetWeight(0);
        });

        var app = builder.Build();
        app.MapGet("/health", async (
            IZLinkLocationReadiness readiness,
            CancellationToken cancellationToken) =>
        {
            var courierNode1Ready = await readiness.IsPeerReadyAsync(
                SampleNames.MeshName,
                ZLinkLocationRole.Spot,
                topology.CourierActorNode1Rid,
                cancellationToken);
            var courierNode2Ready = await readiness.IsPeerReadyAsync(
                SampleNames.MeshName,
                ZLinkLocationRole.Spot,
                topology.CourierActorNode2Rid,
                cancellationToken);

            return courierNode1Ready && courierNode2Ready
                ? Results.Ok(new { ready = true, role = "dispatch" })
                : Results.StatusCode(StatusCodes.Status503ServiceUnavailable);
        });
        app.MapPost("/deliveries", async (
            CreateDeliveryReq request,
            Zlink.Framework.Contracts.Channels.IZLinkRouteClient channels,
            ILoggerFactory loggerFactory,
            CancellationToken cancellationToken) =>
        {
            var assign = new AssignDeliveryMsg(
                request.DeliveryId,
                request.CustomerId,
                request.PickupAddress,
                request.DropoffAddress);
            await channels.SendToChannel(SampleNames.MeshName, SampleNames.DispatchChannel, assign)
                .Async(cancellationToken);
            loggerFactory.CreateLogger("DeliveryDispatch.Server.Dispatch")
                .LogInformation("deliverydispatch api: created delivery={DeliveryId}", request.DeliveryId);
            return Results.Ok(new CreateDeliveryRes(request.DeliveryId));
        });
        return app;
    }
}
