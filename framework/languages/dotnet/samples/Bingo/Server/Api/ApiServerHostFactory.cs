using Bingo.Server.Configuration;
using Bingo.Server.Api.Handlers;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Codecs.Protobuf;

namespace Bingo.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology, SampleApiNode node)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.SetDefaultTimeout(SampleTimings.RequestTimeout);
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableServer(node.ChannelEndpoint)
                .AddHandlerGroup("api");
            options.AddRouteMeshChannel(SampleNames.PlayChannel)
                .EnableServer(node.PlayRouteEndpoint)
                .EnableClient(topology.PlayA.PlayChannelEndpoint)
                .EnableClient(topology.PlayB.PlayChannelEndpoint)
                .SetSendTimeout(TimeSpan.FromSeconds(1))
                .SetRoutingId(node.RouteRid);
        });

        return builder.Build();
    }
}
