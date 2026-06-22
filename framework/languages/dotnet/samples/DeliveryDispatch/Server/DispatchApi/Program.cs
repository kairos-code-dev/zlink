using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;

var topology = SampleTopology.Create();
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls(topology.DispatchApiHttpUrl);
builder.Services.AddSingleton(topology);
builder.Services.AddSingleton<EvidenceStore>();
builder.Services.AddZLinkFramework(options =>
{
    options.ConfigureDispatch().SetMessageDispatchErrorObserver<DeliveryDispatchErrorObserver>();
    options.Codecs.AddJson();
    {
        var channel = options.AddClientServerChannel(SampleNames.DispatchRouteChannel);
                channel.EnableClient(topology.DispatchCenterRouteEndpoint);

    }
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { ready = true, role = "dispatch-api" }));
app.MapPost("/deliveries", async (
    CreateDeliveryRequest request,
    IZLinkChannelClient channels,
    CancellationToken cancellationToken) =>
{
    var assign = new AssignDelivery(
        request.DeliveryId,
        request.CustomerId,
        request.PickupAddress,
        request.DropoffAddress);
    var assigned = await RequestDispatchAsync(channels, assign, cancellationToken);
    Console.Error.WriteLine($"deliverydispatch api: created delivery={assigned.DeliveryId} courier={assigned.CourierId}");
    return Results.Ok(new DeliveryCreated(assigned.DeliveryId));
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
    var lines = evidence.ReadLines();
    return Results.Ok(new ServerAssertionRes(success && reassigned, lines));
});

await app.RunAsync();

static async ValueTask<AssignDeliveryResult> RequestDispatchAsync(
    IZLinkChannelClient channels,
    AssignDelivery request,
    CancellationToken cancellationToken)
{
    Exception? lastError = null;
    for (var attempt = 1; attempt <= 40; attempt++)
    {
        try
        {
            return await channels.RequestToChannel(
                    SampleNames.DispatchRouteChannel,
                    request)
                .Async<AssignDeliveryResult>(cancellationToken);
        }
        catch (Exception error) when (error is not OperationCanceledException)
        {
            lastError = error;
            await Task.Delay(250, cancellationToken);
        }
    }

    throw new InvalidOperationException(
        $"DispatchCenter route was not ready for delivery '{request.DeliveryId}'.",
        lastError);
}
