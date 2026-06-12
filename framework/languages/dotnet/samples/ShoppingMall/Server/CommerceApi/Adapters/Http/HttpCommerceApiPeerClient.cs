using System.Net.Http.Json;
using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;

namespace ShoppingMall.Server.CommerceApi.Adapters.Http;

internal sealed class HttpCommerceApiPeerClient(
    IHttpClientFactory httpClients,
    SampleTopology topology) : ICommerceApiPeerClient
{
    public async ValueTask<StartOrderRes> ForwardStartAsync(
        string ownerInstanceId,
        StartOrderReq request,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForInstance(ownerInstanceId);
        using var client = httpClients.CreateClient();
        client.BaseAddress = new Uri(owner.HttpUrl);
        client.Timeout = SampleTimings.HttpTimeout;
        using var response = await client.PostAsJsonAsync("/orders/start", request, cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<StartOrderRes>(cancellationToken)
               ?? throw new InvalidOperationException("Owner instance returned an empty start response.");
    }
}
