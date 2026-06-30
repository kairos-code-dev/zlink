using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace DeliveryDispatch.Server.Dispatch;

public static class DispatchServerHostFactory
{
    public static WebApplication Build(SampleTopology topology)
    {
        var builder = WebApplication.CreateBuilder();
        builder.WebHost.UseUrls(topology.DispatchHttpUrl);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<EvidenceStore>();
        builder.Services.AddSingleton<DispatchWorkQueue>();
        builder.Services.AddHostedService<DispatchWorker>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("dispatch"))
                .TraceLabel("dispatch");
            options.AddHandlersFromAssemblyOf(typeof(DispatchServerHostFactory));
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.CourierRouteChannel)
                .EnableClient(topology.CourierRouteEndpoint)
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-courier-client"));
            options.AddClientServerChannel(SampleNames.TrackingRouteChannel)
                .EnableClient(topology.TrackingRouteEndpoint)
                .SetRoutingId(Systems.Zlink.RoutingId.From("delivery-dispatch-tracking-client"));
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { ready = true, role = "dispatch" }));
        app.MapPost("/deliveries", async (
            CreateDeliveryRequest request,
            DispatchWorkQueue queue,
            CancellationToken cancellationToken) =>
        {
            var assign = new AssignDelivery(
                request.DeliveryId,
                request.CustomerId,
                request.PickupAddress,
                request.DropoffAddress);
            await queue.EnqueueAsync(assign, cancellationToken);
            Console.Error.WriteLine($"deliverydispatch api: created delivery={request.DeliveryId}");
            return Results.Ok(new DeliveryCreated(request.DeliveryId));
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
