using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Builders;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Compression;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Framing;
using Systems.Zlink.Stream.Connector.Headers;
using Systems.Zlink.Stream.Connector.Metadata;
using Systems.Zlink.Stream.Connector.Options;
using Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.AspNetCore.Builder;

namespace PlayHouseRoomEcho.Server.Play.Handlers;

sealed class CreateRoomHandler(
    IZLinkSpotManager spots,
    SampleSettings settings,
    ILogger<CreateRoomHandler> logger)
{
    [ZLinkRequest]
    public async ValueTask<CreateRoomReply> CreateAsync(
        CreateRoomCommand request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        logger.LogInformation(
            "api -> play: CreateRoomCommand received. room={RoomName}",
            request.RoomName);

        var created = await spots.CreateAsync("room", cancellationToken);
        logger.LogInformation(
            "play: GameRoom spot created. roomId={RoomId}, endpoint={Endpoint}",
            created.SpotRid.ToHex(),
            settings.PlayEndpoint);

        return new CreateRoomReply(
            created.SpotRid.ToHex(),
            settings.PlayEndpoint,
            request.RoomName);
    }
}
