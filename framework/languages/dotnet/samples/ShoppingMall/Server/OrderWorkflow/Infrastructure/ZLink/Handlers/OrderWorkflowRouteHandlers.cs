using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot;
using ShoppingMall.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Handlers;

internal sealed class StartOrderWorkflowRouteHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    WorkflowInstanceTopology instance,
    ILogger<StartOrderWorkflowRouteHandler> logger)
    : IZLinkRouteRequestHandler<StartOrderWorkflowReq, StartOrderWorkflowRes>
{
    public async ValueTask<StartOrderWorkflowRes> HandleAsync(
        StartOrderWorkflowReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "shoppingmall workflow route: StartOrderWorkflowReq order={OrderId}",
            request.OrderId);
        var address = await EnsureSpotAsync(spots, instance, request.OrderId, cancellationToken);
        var response = await routes
            .RequestToSpot(SampleNames.OrderWorkflowRouteChannel, address, request)
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

    internal static async ValueTask<ZLinkSpotAddress> EnsureSpotAsync(
        IZLinkSpotManager spots,
        WorkflowInstanceTopology instance,
        string orderId,
        CancellationToken cancellationToken)
    {
        await spots.GetOrCreateAsync<OrderWorkflowSpot, OrderWorkflowSpotCreateReq>(
            RoutingId.From(orderId),
            new OrderWorkflowSpotCreateReq(orderId),
            cancellationToken);
        return new ZLinkSpotAddress(instance.SpotRid, RoutingId.From(orderId));
    }
}

internal sealed class ContinueOrderWorkflowRouteHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    WorkflowInstanceTopology instance)
    : IZLinkRouteRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes>
{
    public async ValueTask<ContinueOrderWorkflowRes> HandleAsync(
        ContinueOrderWorkflowReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        var address = await StartOrderWorkflowRouteHandler.EnsureSpotAsync(
            spots,
            instance,
            request.OrderId,
            cancellationToken);
        return await routes
            .RequestToSpot(SampleNames.OrderWorkflowRouteChannel, address, request)
            .Async<ContinueOrderWorkflowRes>(cancellationToken);
    }
}

internal sealed class RebuildOrderProjectionRouteHandler(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    WorkflowInstanceTopology instance,
    ILogger<RebuildOrderProjectionRouteHandler> logger)
    : IZLinkRouteRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes>
{
    public async ValueTask<RebuildOrderProjectionRes> HandleAsync(
        RebuildOrderProjectionReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        var address = await StartOrderWorkflowRouteHandler.EnsureSpotAsync(
            spots,
            instance,
            request.OrderId,
            cancellationToken);
        var response = await routes
            .RequestToSpot(SampleNames.OrderWorkflowRouteChannel, address, request)
            .Async<RebuildOrderProjectionRes>(cancellationToken);
        logger.LogInformation(
            "shoppingmall order: projection rebuilt order={OrderId} status={Status}",
            response.State.OrderId,
            response.State.Status);
        return response;
    }
}
