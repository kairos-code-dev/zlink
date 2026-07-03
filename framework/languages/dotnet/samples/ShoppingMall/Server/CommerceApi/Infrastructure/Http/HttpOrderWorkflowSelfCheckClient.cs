using System.Net.Http.Json;
using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;

namespace ShoppingMall.Server.CommerceApi.Infrastructure.Http;

internal sealed class HttpOrderWorkflowSelfCheckClient(SampleTopology topology) : IOrderWorkflowSelfCheckClient
{
    public async ValueTask<OrderState> PrepareInventoryReservedCheckpointAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        using var http = new HttpClient { Timeout = SampleTimings.HttpTimeout };
        var owner = topology.ForOrderId(command.OrderId);
        var response = await http.PostAsJsonAsync(
            $"{owner.HttpUrl}/self-check/workflow/inventory-reserved",
            command,
            cancellationToken);
        response.EnsureSuccessStatusCode();
        var workflow = await response.Content.ReadFromJsonAsync<StartOrderWorkflowRes>(
                           cancellationToken)
                       ?? throw new InvalidOperationException("Workflow self-check returned an empty response.");
        return workflow.State;
    }
}
