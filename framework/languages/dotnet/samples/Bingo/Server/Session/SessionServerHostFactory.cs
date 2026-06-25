using Bingo.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;

namespace Bingo.Server.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode session)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(session);
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("session"))
                .TraceLabel("session");
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();
            options.AddRouteMesh(SampleNames.PlayChannel)
                .EnableServer(session.PlayRouteEndpoint)
                .EnableClient()
                .SetRoutingId(session.PlayRouteRid);
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery)
                .EnableRouter(session.RouterEndpoint)
                .SetRoutingId(session.RoutingId);
            options.AddStreamNode(SampleNames.StreamNode)
                .Bind(session.StreamEndpoint)
                .RegisterSession<Sessions.BingoSession>();
        });

        return builder.Build();
    }
}
