using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Adapters.ZLink.Actors;
using TicTacToe.Server.Play.Adapters.ZLink.Handlers;
using TicTacToe.Server.Play.Adapters.ZLink.Sessions;
using TicTacToe.Server.Play.Adapters.ZLink.Spots;
using TicTacToe.Server.Play.Application.GameCreation;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.AspNetCore;

namespace TicTacToe.Server.Play;

internal sealed class PlayServer(SampleSettings settings)
{
    public IHost Build()
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(builder.Logging, settings, "play");

        builder.Services.AddSingleton(settings);
        builder.Services.AddSingleton<IRoomRouteStore, RedisRoomRouteStore>();
        builder.Services.AddSingleton<TicTacToeGameCreator>();

        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleTimeouts.Request;
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<TicTacToeDispatchErrorObserver>();
            options.AddHandlersFromAssemblyOf(typeof(PlayServer));
            options.Codecs.AddJson();
            options.AddActorFactory<PlayActorFactory>(SampleTypes.PlayerActor);
            options.AddSpotRemoteAddressResolver<RedisSpotRemoteAddressResolver>();

            options.AddClientServerChannel(SampleChannels.Api)
                .EnableClient(settings.ApiChannelEndpoints[0])
                .EnableClient(settings.ApiChannelEndpoints[1]);

            options.AddClientServerChannel(SampleChannels.Play(settings.PlayIndex))
                .EnableServer(settings.PlayChannelEndpoint)
                .AddRequestHandler<CreateGameHandler>();

            options.AddStreamNode(SampleNodes.ClientStream)
                .AttachActorGateway(SampleNodes.PlaySpot)
                .Bind(settings.PlayEndpoint)
                .RegisterSession<PlaySession>();

            options.AddSpotMesh(SampleNodes.PlaySpot)
                .EnableRouter(settings.SpotEndpoint)
                .SetRouterRoutingId(RoutingId.From(settings.PlaySpotNodeRid))
                .ConnectRouter(
                    RoutingId.From(settings.PeerPlaySpotNodeRid),
                    settings.PeerSpotEndpoint)
                .EnablePubSub(settings.SpotPubSubEndpoint)
                .ConnectPeerPub(settings.PeerSpotPubEndpoint)
                .AddEntrySpot<PlayEntrySpot>()
                .AddSpotFactory<TicTacToeGame>();
        });

        return builder.Build();
    }
}
