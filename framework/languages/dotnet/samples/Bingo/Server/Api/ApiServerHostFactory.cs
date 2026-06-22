using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;

namespace Bingo.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology, SampleApiNode node)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<BingoDispatchErrorObserver>();
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableServer(node.ChannelEndpoint)
                .AddHandlerGroup("api");
            options.AddRouteMeshChannel(SampleNames.PlayChannel)
                .EnableServer(node.PlayRouteEndpoint)
                .EnableClient()
                .SetRoutingId(node.RouteRid);
        });

        return builder.Build();
    }
}
