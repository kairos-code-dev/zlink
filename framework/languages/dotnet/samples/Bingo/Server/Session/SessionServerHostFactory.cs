using Systems.Zlink;
using Systems.Zlink.Codecs.Protobuf;
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

namespace Bingo.Server.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode session)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton(topology);
        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimings.RequestTimeout;
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.AddProtobuf();
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var channel = options.AddClientServerChannel(SampleNames.ApiChannel);
                channel.EnableClient();

            }
            {
                var channel = options.AddClientServerChannel(SampleNames.PlayChannel);
                channel.EnableClient();

            }
            {
                var mesh = options.AddSpotMesh(SampleNames.RoomSpotDiscovery);
                {
                    var node = mesh.AddNode(SampleNames.SessionSpotNode);
                    {
                        var router = node.EnableRouter(session.RouterEndpoint);
                        router.SetRouterRoutingId(session.RouterRoutingId);

                    }
                    {
                        var pubsub = node.EnablePubSub(session.PubEndpoint);
                        pubsub.SetPubSubRoutingId(session.PubRoutingId);

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
