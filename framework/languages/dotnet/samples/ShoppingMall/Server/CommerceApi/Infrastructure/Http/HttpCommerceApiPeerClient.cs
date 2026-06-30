using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.HttpClient;

namespace ShoppingMall.Server.CommerceApi.Infrastructure.Http;

internal sealed class HttpCommerceApiPeerClient(SampleTopology topology) : ICommerceApiPeerClient
{
    public async ValueTask<StartOrderRes> ForwardStartAsync(
        string ownerInstanceId,
        StartOrderReq request,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForInstance(ownerInstanceId);
        using var client = ZLinkHttpClient.Create(owner.HttpUrl)
            .Json()
            .Timeout(SampleTimings.HttpTimeout)
            .Build();
        return (await client.Post("/orders/start")
            .Body(request)
            .SubmitAsync<StartOrderRes>(cancellationToken)).Body;
    }
}