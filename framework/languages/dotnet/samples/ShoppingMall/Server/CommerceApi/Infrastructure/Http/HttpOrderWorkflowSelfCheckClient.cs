using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.HttpClient;

namespace ShoppingMall.Server.CommerceApi.Infrastructure.Http;

internal sealed class HttpOrderWorkflowSelfCheckClient(SampleTopology topology) : IOrderWorkflowSelfCheckClient
{
    public async ValueTask<OrderState> PrepareInventoryReservedCheckpointAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForOrderId(command.OrderId);
        using var client = ZLinkHttpClient.Create(owner.HttpUrl)
            .Timeout(SampleTimings.HttpTimeout)
            .Build();
        var workflow = (await client.Post("/self-check/workflow/inventory-reserved")
            .Body(command)
            .SubmitAsync<StartOrderWorkflowRes>(cancellationToken)).Body;
        return workflow.State;
    }
}
