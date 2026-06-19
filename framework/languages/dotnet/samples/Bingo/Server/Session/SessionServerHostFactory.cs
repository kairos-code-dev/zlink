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
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            {
                var channel = options.AddClientServerChannel(SampleNames.ApiChannel);
                channel.EnableClient(topology.ApiA.ChannelEndpoint);
                channel.EnableClient(topology.ApiB.ChannelEndpoint);
                channel.ConfigureClientSocket().SendTimeout = TimeSpan.FromSeconds(1);

            }
            {
                var channel = options.AddRouteMeshChannel(SampleNames.PlayChannel);
                channel.EnableServer(session.PlayRouteEndpoint);
                channel.EnableClient(session.PreferredPlayChannelEndpoint);
                channel.ConfigureSocket().SendTimeout = TimeSpan.FromSeconds(1);
                channel.ConfigureRouting().RoutingId = session.PlayRouteRid;

            }
            {
                var mesh = options.AddSpotMesh(SampleNames.RoomSpotDiscovery);
                mesh.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
                {
                    var node = mesh.AddNode(SampleNames.SessionSpotNode);
                    {
                        var router = node.EnableRouter(session.RouterEndpoint);
                        router.SetRouterRoutingId(session.RouterRoutingId);

                    }

                }

            }
            {
                var stream = options.AddStreamNode(SampleNames.StreamNode);
                stream.AttachActorGateway(SampleNames.SessionSpotNode);
                stream.Bind(session.StreamEndpoint);
                stream.RegisterSession<Sessions.BingoSession>();

            }
        });

        return builder.Build();
    }
}
