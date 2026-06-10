using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using ShoppingMallCheckout.Server.OrderWorkflow.Adapters.ZLink.Spots;
using ShoppingMallCheckout.Server.OrderWorkflow.Application.CheckoutWorkflow;
using ShoppingMallCheckout.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMallCheckout.Server.OrderWorkflow.Adapters.ZLink.Handlers;

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
        Console.Error.WriteLine($"shoppingmall workflow route: StartOrderWorkflowReq order={request.OrderId}");
        await EnsureSpotAsync(spots, request.OrderId, cancellationToken);
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
        Console.Error.WriteLine($"shoppingmall order: started order={state.OrderId} status={state.Status}");
        return new StartOrderWorkflowRes(state);
    }

    internal static async ValueTask EnsureSpotAsync(
        IZLinkSpotManager spots,
        string orderId,
        CancellationToken cancellationToken)
    {
        using var payload = new OrderWorkflowSpotCreateReq(orderId).ToJson();
        ZLinkSpotCreateResult result;
        try
        {
            result = await spots.GetOrCreateAsync<OrderWorkflowSpot>(
                RoutingId.From(orderId),
                payload,
                cancellationToken);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"shoppingmall workflow route: spot create failed order={orderId} error={error}");
            throw;
        }
        if (result.State == ZLinkSpotCreateState.Rejected)
        {
            throw new InvalidOperationException($"Order workflow spot '{orderId}' was rejected.");
        }
    }

    private static async Task ContinueAfterStartAsync(
        OrderWorkflowService workflow,
        string orderId)
    {
        await Task.Delay(150).ConfigureAwait(false);
        await workflow.ContinueAsync(new ContinueOrderWorkflowReq(orderId), CancellationToken.None).ConfigureAwait(false);
    }
}

internal sealed class ContinueOrderWorkflowRouteHandler(
    IZLinkSpotManager spots,
    OrderWorkflowService workflow)
    : IZLinkRouteRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes>
{
    public async ValueTask<ContinueOrderWorkflowRes> HandleAsync(
        ContinueOrderWorkflowReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        await StartOrderWorkflowRouteHandler.EnsureSpotAsync(spots, request.OrderId, cancellationToken);
        var state = await workflow.ContinueAsync(request, cancellationToken);
        return new ContinueOrderWorkflowRes(state);
    }
}

internal sealed class RebuildOrderProjectionRouteHandler(
    IZLinkSpotManager spots,
    OrderWorkflowService workflow)
    : IZLinkRouteRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes>
{
    public async ValueTask<RebuildOrderProjectionRes> HandleAsync(
        RebuildOrderProjectionReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        await StartOrderWorkflowRouteHandler.EnsureSpotAsync(spots, request.OrderId, cancellationToken);
        var state = await workflow.RebuildProjectionAsync(request.OrderId, cancellationToken);
        Console.Error.WriteLine($"shoppingmall order: projection rebuilt order={state.OrderId} status={state.Status}");
        return new RebuildOrderProjectionRes(state);
    }
}
