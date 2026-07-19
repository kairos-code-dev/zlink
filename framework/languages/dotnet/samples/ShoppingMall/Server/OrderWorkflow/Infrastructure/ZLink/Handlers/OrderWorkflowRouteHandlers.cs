using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot;
using ShoppingMall.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("order-workflow")]
internal sealed class StartOrderWorkflowRouteHandler(
    IZLinkSpotManager spots,
    IZLinkSpotClient routes,
    IZLinkSpotHandleResolver spotHandles,
    ILogger<StartOrderWorkflowRouteHandler> logger)
    : IZLinkRequestHandler<StartOrderWorkflowReq, StartOrderWorkflowRes>
{
    public async ValueTask<StartOrderWorkflowRes> HandleAsync(
        StartOrderWorkflowReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "shoppingmall workflow route: StartOrderWorkflowReq order={OrderId}",
            request.OrderId);
        var address = await EnsureSpotAsync(spots, spotHandles, request.OrderId, cancellationToken);
        var response = await routes
            .RequestToSpot(address, request)
            .Async<StartOrderWorkflowRes>(cancellationToken);
        logger.LogInformation(
            "shoppingmall workflow route: delivered StartOrderWorkflowReq to spot owner. order={OrderId}, status={Status}",
            response.State.OrderId,
            response.State.Status);
        logger.LogInformation(
            "shoppingmall order: started order={OrderId} status={Status}",
            response.State.OrderId,
            response.State.Status);
        return response;
    }

    internal static async ValueTask<SpotHandle> EnsureSpotAsync(
        IZLinkSpotManager spots,
        IZLinkSpotHandleResolver spotHandles,
        string orderId,
        CancellationToken cancellationToken)
    {
        await spots.GetOrCreateAsync<OrderWorkflowSpot, OrderWorkflowSpotCreateReq>(
            RoutingId.From(orderId),
            new OrderWorkflowSpotCreateReq(orderId),
            cancellationToken);
        return await spotHandles.ResolveSpotHandleAsync(RoutingId.From(orderId), cancellationToken)
               ?? throw new InvalidOperationException($"Order workflow spot '{orderId}' was not found.");
    }
}

[ZLinkHandlerGroup("order-workflow")]
internal sealed class ContinueOrderWorkflowRouteHandler(
    IZLinkSpotManager spots,
    IZLinkSpotClient routes,
    IZLinkSpotHandleResolver spotHandles)
    : IZLinkRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes>
{
    public async ValueTask<ContinueOrderWorkflowRes> HandleAsync(
        ContinueOrderWorkflowReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var address = await StartOrderWorkflowRouteHandler.EnsureSpotAsync(
            spots,
            spotHandles,
            request.OrderId,
            cancellationToken);
        return await routes
            .RequestToSpot(address, request)
            .Async<ContinueOrderWorkflowRes>(cancellationToken);
    }
}

[ZLinkHandlerGroup("order-workflow")]
internal sealed class RebuildOrderProjectionRouteHandler(
    IZLinkSpotManager spots,
    IZLinkSpotClient routes,
    IZLinkSpotHandleResolver spotHandles,
    ILogger<RebuildOrderProjectionRouteHandler> logger)
    : IZLinkRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes>
{
    public async ValueTask<RebuildOrderProjectionRes> HandleAsync(
        RebuildOrderProjectionReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var address = await StartOrderWorkflowRouteHandler.EnsureSpotAsync(
            spots,
            spotHandles,
            request.OrderId,
            cancellationToken);
        var response = await routes
            .RequestToSpot(address, request)
            .Async<RebuildOrderProjectionRes>(cancellationToken);
        logger.LogInformation(
            "shoppingmall order: projection rebuilt order={OrderId} status={Status}",
            response.State.OrderId,
            response.State.Status);
        return response;
    }
}

[ZLinkHandlerGroup("order-workflow")]
internal sealed class PrepareInventoryReservedCheckpointRouteHandler(
    IZLinkSpotManager spots,
    IZLinkSpotClient routes,
    IZLinkSpotHandleResolver spotHandles)
    : IZLinkRequestHandler<PrepareInventoryReservedCheckpointReq, StartOrderWorkflowRes>
{
    public async ValueTask<StartOrderWorkflowRes> HandleAsync(
        PrepareInventoryReservedCheckpointReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var address = await StartOrderWorkflowRouteHandler.EnsureSpotAsync(
            spots,
            spotHandles,
            request.Command.OrderId,
            cancellationToken);
        return await routes
            .RequestToSpot(address, request)
            .Async<StartOrderWorkflowRes>(cancellationToken);
    }
}
