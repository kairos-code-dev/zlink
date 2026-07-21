using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;
using ShoppingMall.Server.OrderWorkflow.Application.SelfCheck;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot;

internal sealed class OrderWorkflowSpot(
    IZLinkSpotContext context,
    WorkflowInstanceTopology instance,
    OrderWorkflowService workflow,
    OrderWorkflowSelfCheckService selfChecks,
    ILogger<OrderWorkflowSpot> logger) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "shoppingmall order spot: ready. order={OrderId}, spot={SpotRid}",
            Context.SpotRid.ToString(),
            Context.SpotRid.ToString());
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public async ValueTask<StartOrderWorkflowRes> StartOrderWorkflowAsync(
        StartOrderWorkflowReq request,
        CancellationToken cancellationToken)
    {
        var state = await workflow.StartAndContinueAsync(request, cancellationToken);
        logger.LogInformation(
            "shoppingmall order: started. order={OrderId}, status={Status}",
            state.OrderId,
            state.Status);
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

    public async ValueTask<StartOrderWorkflowRes> PrepareInventoryReservedCheckpointAsync(
        PrepareInventoryReservedCheckpointReq request,
        CancellationToken cancellationToken)
    {
        var state = await selfChecks.PrepareInventoryReservedCheckpointAsync(request.Command, cancellationToken);
        logger.LogInformation(
            "shoppingmall order: inventory reserved. order={OrderId}, status={Status}",
            state.OrderId,
            state.Status);
        await Context.Outbound.Publish(
                SampleNames.OrderProjectionChannel,
                SampleNames.OrderProjectionTopic,
                new OrderProjectionUpdatedEvent(
                    state.OrderId,
                    state.Status,
                    instance.InstanceId))
            .SubmitAsync(cancellationToken);
        return new StartOrderWorkflowRes(state);
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
        return new RebuildOrderProjectionRes(state);
    }

}

internal sealed record OrderWorkflowSpotCreateReq(string OrderId);
