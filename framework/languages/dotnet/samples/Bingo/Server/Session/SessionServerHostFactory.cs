using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Session.Sessions;
using Bingo.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;

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
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<BingoDispatchErrorObserver>();
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();
            options.AddRouteMeshChannel(SampleNames.PlayChannel)
                .EnableServer(session.PlayRouteEndpoint)
                .EnableClient()
                .SetRoutingId(session.PlayRouteRid);
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery)
                .AddNode(SampleNames.SessionSpotNode)
                .EnableRouter(session.RouterEndpoint)
                .SetRouterRoutingId(session.RouterRoutingId);
            options.AddStreamNode(SampleNames.StreamNode)
                .AttachActorGateway(SampleNames.SessionSpotNode)
                .Bind(session.StreamEndpoint)
                .RegisterSession<Sessions.BingoSession>();
        });

        return builder.Build();
    }
}
