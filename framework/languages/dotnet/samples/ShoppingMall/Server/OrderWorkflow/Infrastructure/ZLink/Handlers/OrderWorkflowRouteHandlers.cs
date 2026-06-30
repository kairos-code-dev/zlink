using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot;
using ShoppingMall.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Handlers;

internal sealed class StartOrderWorkflowRouteHandler(
    IZLinkSpotManager spots,
    OrderWorkflowService workflow,
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
        await EnsureSpotAsync(spots, request.OrderId, logger, cancellationToken);
        var state = await workflow.StartAsync(request, cancellationToken);
        _ = ContinueAfterStartAsync(workflow, request.OrderId)
            .ContinueWith(
                static task => _ = task.Exception,
                CancellationToken.None,
                TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        logger.LogInformation(
            "shoppingmall workflow route: delivered StartOrderWorkflowReq to spot owner. order={OrderId}, status={Status}",
            state.OrderId,
            state.Status);
        logger.LogInformation(
            "shoppingmall order: started order={OrderId} status={Status}",
            state.OrderId,
            state.Status);
        return new StartOrderWorkflowRes(state);
    }

    internal static async ValueTask EnsureSpotAsync(
        IZLinkSpotManager spots,
        string orderId,
        ILogger logger,
        CancellationToken cancellationToken)
    {
        try
        {
            await spots.GetOrCreateAsync<OrderWorkflowSpot>(
                RoutingId.From(orderId),
                new OrderWorkflowSpotCreateReq(orderId),
                cancellationToken);
        }
        catch (Exception error)
        {
            logger.LogError(
                error,
                "shoppingmall workflow route: spot create failed order={OrderId}",
                orderId);
            throw;
        }
    }

    private static async Task ContinueAfterStartAsync(
        OrderWorkflowService workflow,
        string orderId)
    {
        await workflow.ContinueAsync(new ContinueOrderWorkflowReq(orderId), CancellationToken.None)
            .ConfigureAwait(false);
    }
}

internal sealed class ContinueOrderWorkflowRouteHandler(
    IZLinkSpotManager spots,
    OrderWorkflowService workflow,
    ILogger<ContinueOrderWorkflowRouteHandler> logger)
    : IZLinkRouteRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes>
{
    public async ValueTask<ContinueOrderWorkflowRes> HandleAsync(
        ContinueOrderWorkflowReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        await StartOrderWorkflowRouteHandler.EnsureSpotAsync(spots, request.OrderId, logger, cancellationToken);
        var state = await workflow.ContinueAsync(request, cancellationToken);
        return new ContinueOrderWorkflowRes(state);
    }
}

internal sealed class RebuildOrderProjectionRouteHandler(
    IZLinkSpotManager spots,
    OrderWorkflowService workflow,
    ILogger<RebuildOrderProjectionRouteHandler> logger)
    : IZLinkRouteRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes>
{
    public async ValueTask<RebuildOrderProjectionRes> HandleAsync(
        RebuildOrderProjectionReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        await StartOrderWorkflowRouteHandler.EnsureSpotAsync(spots, request.OrderId, logger, cancellationToken);
        var state = await workflow.RebuildProjectionAsync(request.OrderId, cancellationToken);
        logger.LogInformation(
            "shoppingmall order: projection rebuilt order={OrderId} status={Status}",
            state.OrderId,
            state.Status);
        return new RebuildOrderProjectionRes(state);
    }
}
