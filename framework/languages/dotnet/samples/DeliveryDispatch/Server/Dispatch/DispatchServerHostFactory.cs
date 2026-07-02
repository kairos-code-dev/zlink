using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.AspNetCore;
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
        builder.Services.AddHostedService<DispatchWorker>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddRedisLocationStore(redis =>
            {
                redis.ConnectionString = topology.RedisEndpoint;
                redis.KeyPrefix = topology.RedisKeyPrefix;
            });
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("dispatch"))
                .TraceLabel("dispatch");
            options.AddHandlersFromAssemblyOf(typeof(DispatchServerHostFactory));
            options.AddClientServerChannel(SampleNames.CourierRouteChannel)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-courier-client"));
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableClient()
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-tracking-client"));
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { ready = true, role = "dispatch" }));
        app.MapPost("/deliveries", async (
            CreateDeliveryReq request,
            DispatchWorkQueue queue,
            ILoggerFactory loggerFactory,
            CancellationToken cancellationToken) =>
        {
            var assign = new AssignDelivery(
                request.DeliveryId,
                request.CustomerId,
                request.PickupAddress,
                request.DropoffAddress);
            await queue.EnqueueAsync(assign, cancellationToken);
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
