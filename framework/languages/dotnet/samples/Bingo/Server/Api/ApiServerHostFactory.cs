using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;

namespace Bingo.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology, SampleApiNode node)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .SetMessageDispatchErrorObserver<BingoDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("api"))
                .TraceNodeId("api");
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableServer(node.ChannelEndpoint)
                .AddHandlerGroup("api");
            options.AddRouteMesh(SampleNames.PlayChannel)
                .EnableServer(node.PlayRouteEndpoint)
                .EnableClient()
                .SetRoutingId(node.RouteRid);
        });

        return builder.Build();
    }
}
