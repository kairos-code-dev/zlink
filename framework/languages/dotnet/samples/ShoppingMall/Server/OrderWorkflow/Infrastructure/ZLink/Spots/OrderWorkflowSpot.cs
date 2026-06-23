using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots;

internal sealed class OrderWorkflowSpot(
    IZLinkSpotContext context,
    OrderWorkflowService workflow,
    ILogger<OrderWorkflowSpot> logger) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddPacket<Handlers.StartOrderWorkflowHandler>();
        Context.Handlers.AddPacket<Handlers.ContinueOrderWorkflowHandler>();
        Context.Handlers.AddPacket<Handlers.RebuildOrderProjectionHandler>();
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "shoppingmall order spot: ready. order={OrderId}, spot={SpotRid}",
            Context.SpotRid.ToString(),
            Context.SpotRid.ToString());
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask<StartOrderWorkflowRes> StartOrderWorkflowAsync(
        StartOrderWorkflowReq request,
        CancellationToken cancellationToken)
    {
        var state = await workflow.StartAsync(request, cancellationToken);
        logger.LogInformation(
            "shoppingmall order: started. order={OrderId}, status={Status}",
            state.OrderId,
            state.Status);
        Console.Error.WriteLine($"shoppingmall order: started order={state.OrderId} status={state.Status}");
        return new StartOrderWorkflowRes(state);
    }

    public async ValueTask<ContinueOrderWorkflowRes> ContinueOrderWorkflowAsync(
        ContinueOrderWorkflowReq request,
        CancellationToken cancellationToken)
    {
        var state = await workflow.ContinueAsync(request, cancellationToken);
        logger.LogInformation(
            "shoppingmall order: continued. order={OrderId}, status={Status}",
            state.OrderId,
            state.Status);
        return new ContinueOrderWorkflowRes(state);
    }

    public async ValueTask<RebuildOrderProjectionRes> RebuildOrderProjectionAsync(
        RebuildOrderProjectionReq request,
        CancellationToken cancellationToken)
    {
        var state = await workflow.RebuildProjectionAsync(request.OrderId, cancellationToken);
        logger.LogInformation(
            "shoppingmall order: projection rebuilt. order={OrderId}, status={Status}",
            state.OrderId,
            state.Status);
        Console.Error.WriteLine($"shoppingmall order: projection rebuilt order={state.OrderId} status={state.Status}");
        return new RebuildOrderProjectionRes(state);
    }
}

internal sealed record OrderWorkflowSpotCreateReq(string OrderId);
