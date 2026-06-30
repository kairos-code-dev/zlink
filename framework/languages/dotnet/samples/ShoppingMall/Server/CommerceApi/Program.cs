using ShoppingMall.Server.CommerceApi.Application.OrderWorkflow;
using ShoppingMall.Server.CommerceApi.Infrastructure.Http;
using ShoppingMall.Server.CommerceApi.Infrastructure.ZLink;
using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.Shared.Domain;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Server.Shared.Store;
using ShoppingMall.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace ShoppingMall.Server.CommerceApi;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var instanceId = ReadOption(args, "--instance") ??
                         Environment.GetEnvironmentVariable("SHOPPINGMALL_INSTANCE") ?? "api-a";
        var topology = SampleTopology.Create();
        var instance = topology.ForInstance(instanceId);
        var builder = WebApplication.CreateBuilder(args);
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("SHOPPINGMALL_LOG_DIR"),
            instance.InstanceId);

        builder.WebHost.UseUrls(instance.HttpUrl);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(new CommerceApiInstanceOptions(instance.InstanceId));
        builder.Services.AddSingleton(new FileCommerceStores(topology.StoreDirectory));
        builder.Services.AddSingleton<IOrderReadModelStore>(static provider =>
            provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<ICommerceStateStore>(static provider =>
            provider.GetRequiredService<FileCommerceStores>());
        builder.Services.AddSingleton<IOrderWorkflowRouter, ZLinkOrderWorkflowRouter>();
        builder.Services.AddSingleton<ICommerceApiPeerClient, HttpCommerceApiPeerClient>();
        builder.Services.AddSingleton<StartOrderUseCase>();
        builder.Services.AddSingleton<GetOrderStateUseCase>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(instance.InstanceId))
                .TraceLabel(instance.InstanceId);
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddRouteMesh(SampleNames.OrderWorkflowRouteChannel)
                .EnableServer(instance.RouteEndpoint)
                .SetRoutingId(instance.RouteRid);
        });

        var app = builder.Build();
        app.Services.GetRequiredService<FileCommerceStores>().SeedDefaults();

        app.MapGet("/health", () => Results.Ok(new { ready = true, instance = instance.InstanceId }));
        app.MapPost("/orders/start", async (
            StartOrderReq request,
            StartOrderUseCase useCase,
            CancellationToken cancellationToken) =>
        {
            var response = await useCase.ExecuteAsync(request, cancellationToken);
            return Results.Ok(response);
        });
        app.MapGet("/orders/{orderId}", async (
            string orderId,
            GetOrderStateUseCase useCase,
            CancellationToken cancellationToken) =>
        {
            var response = await useCase.ExecuteAsync(new GetOrderStateReq(orderId), cancellationToken);
            return Results.Ok(response);
        });
        app.MapPost("/self-check/projection/{orderId}/delete", async (
            string orderId,
            IOrderReadModelStore readModels,
            CancellationToken cancellationToken) =>
        {
            await readModels.DeleteAsync(orderId, cancellationToken);
            return Results.Ok();
        });
        app.MapPost("/self-check/projection/{orderId}/rebuild", async (
            string orderId,
            IOrderWorkflowRouter workflows,
            ILoggerFactory loggerFactory,
            CancellationToken cancellationToken) =>
        {
            var state = await workflows.RebuildProjectionAsync(
                new RebuildOrderProjectionReq(orderId),
                cancellationToken);
            loggerFactory.CreateLogger("ShoppingMall.Server.CommerceApi")
                .LogInformation(
                    "shoppingmall projection: rebuilt order={OrderId} status={Status}",
                    state.OrderId,
                    state.Status);
            return Results.Ok(new RebuildOrderProjectionRes(state));
        });
        app.MapPost("/self-check/idempotency/pending", async (
            PendingMappingHttpReq request,
            ICommerceStateStore commerce,
            CancellationToken cancellationToken) =>
        {
            await commerce.CreatePendingMappingAsync(
                request.IdempotencyKey,
                request.OrderId,
                request.OwnerInstanceId,
                cancellationToken);
            return Results.Ok();
        });
        app.MapPost("/self-check/assert", async (
            ServerAssertionReq request,
            FileCommerceStores stores,
            ILoggerFactory loggerFactory,
            CancellationToken cancellationToken) =>
        {
            var evidence = await stores.EvidenceAsync(
                [
                    request.SuccessfulOrderId,
                    request.PendingRecoveredOrderId,
                    request.InventoryFailureOrderId,
                    request.PaymentFailureOrderId,
                    request.ScaleOutOrderId
                ],
                cancellationToken);
            var lines = evidence.EventsByOrder
                .Select(item => $"{item.Key}:{string.Join(">", item.Value)}")
                .Append($"paymentFailures={evidence.PaymentFailureCount}")
                .Append($"releasedReservations={evidence.ReleasedReservationCount}")
                .Append($"startedIdempotency={evidence.StartedIdempotencyCount}")
                .ToArray();
            var passed =
                HasSequence(evidence, request.SuccessfulOrderId,
                    nameof(OrderStartedEvent),
                    nameof(InventoryReservedEvent),
                    nameof(PaymentAuthorizedEvent),
                    nameof(OrderConfirmedEvent))
                && HasPrefix(evidence, request.PendingRecoveredOrderId,
                    nameof(OrderStartedEvent))
                && HasSequence(evidence, request.InventoryFailureOrderId,
                    nameof(OrderStartedEvent),
                    nameof(InventoryReservationFailedEvent),
                    nameof(OrderFailedEvent))
                && HasSequence(evidence, request.PaymentFailureOrderId,
                    nameof(OrderStartedEvent),
                    nameof(InventoryReservedEvent),
                    nameof(PaymentFailedEvent),
                    nameof(InventoryReleasedEvent),
                    nameof(OrderFailedEvent))
                && HasSequence(evidence, request.ScaleOutOrderId,
                    nameof(OrderStartedEvent),
                    nameof(InventoryReservedEvent),
                    nameof(PaymentAuthorizedEvent),
                    nameof(OrderConfirmedEvent))
                && evidence.ReleasedReservationCount >= 1
                && evidence.PaymentFailureCount >= 1
                && evidence.StartedIdempotencyCount == 5;
            loggerFactory.CreateLogger("ShoppingMall.Server.CommerceApi")
                .LogInformation("shoppingmall evidence: {Evidence}", string.Join("; ", lines));
            return Results.Ok(new ServerAssertionRes(passed, lines));
        });

        await app.RunAsync();
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }

    private static bool HasSequence(
        StoreEvidence evidence,
        string orderId,
        params string[] expected)
    {
        return evidence.EventsByOrder.TryGetValue(orderId, out var actual)
               && actual.SequenceEqual(expected);
    }

    private static bool HasPrefix(
        StoreEvidence evidence,
        string orderId,
        params string[] expected)
    {
        return evidence.EventsByOrder.TryGetValue(orderId, out var actual)
               && actual.Length >= expected.Length
               && actual.Take(expected.Length).SequenceEqual(expected);
    }
}

internal sealed record PendingMappingHttpReq(
    string IdempotencyKey,
    string OrderId,
    string OwnerInstanceId);
