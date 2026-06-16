using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Adapters.ZLink.Actors;
using TicTacToe.Server.Play.Adapters.ZLink.Handlers;
using TicTacToe.Server.Play.Adapters.ZLink.Sessions;
using TicTacToe.Server.Play.Adapters.ZLink.Spots;
using TicTacToe.Server.Play.Application.GameCreation;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.Server.Play;

internal sealed class PlayServer(SampleSettings settings)
{
    public IHost Build()
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(builder.Logging, settings, "play");

        builder.Services.AddSingleton(settings);
        builder.Services.AddSingleton<TicTacToeGameCreator>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimeouts.Request;
            options.Codecs.AddJson();
            options.AddActorFactory<PlayActorFactory>(SampleTypes.PlayerActor);

            options.AddClientServerChannel(SampleChannels.Api, channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections =>
                    {
                        connections.Connect(settings.ApiChannelEndpoint);
                    });
                });
            });

            options.AddClientServerChannel(SampleChannels.Play, channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(settings.PlayChannelEndpoint);
                });
                channel.AddRequestHandler<CreateGameHandler>();
            });

            options.AddRouteMeshChannel(SampleChannels.Router, routed =>
            {
                routed.Bind(settings.PlayRouterEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = RoutingId.From(SampleTypes.PlayRouterId));
                routed.UseManualConnections(connections => connections.Connect(settings.PlayRouterEndpoint));
            });

            options.AddStreamNode(SampleNodes.ClientStream, stream =>
            {
                stream.AttachActorGateway(SampleNodes.PlaySpot);
                stream.Bind(settings.PlayEndpoint);
                stream.RegisterSession<PlaySession>();
            });

            options.AddSpotMesh(SampleNodes.PlaySpot, mesh =>
            {
                mesh.AddNode(SampleNodes.PlaySpot, spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(settings.SpotEndpoint);
                        router.SetRoutingId(RoutingId.From(SampleTypes.PlaySpotNodeId));
                    });
                    spot.AddEntrySpot<PlayEntrySpot>();
                    spot.AddSpotFactory<TicTacToeGame>();
                });
            });
        });

        return builder.Build();
    }
}
