using TicTacToe.SessionGateway.Configuration;
using TicTacToe.SessionGateway.Contracts;
using Zlink;
using Zlink.Codecs.Json;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionGateway.Play;

internal sealed class PlaySessionProxy(
    TicTacToeGameService games,
    PlayerSessionDirectory sessions) : IZLinkSessionProxyHandler
{
    public async ValueTask<Message?> HandleAsync(
        IZLinkSessionProxyContext context,
        ZLinkActorRelayMessage message,
        CancellationToken cancellationToken)
    {
        return message.Envelope.StreamHeader.Name switch
        {
            nameof(JoinGameReq) => await HandleJoinAsync(context, message, cancellationToken).ConfigureAwait(false),
            nameof(PlaceMarkReq) => await HandlePlaceMarkAsync(context, message, cancellationToken).ConfigureAwait(false),
            _ => throw new InvalidOperationException(
                $"Unsupported game packet '{message.Envelope.StreamHeader.Name}'.")
        };
    }

    private async ValueTask<Message> HandleJoinAsync(
        IZLinkSessionProxyContext context,
        ZLinkActorRelayMessage message,
        CancellationToken cancellationToken)
    {
        var request = message.Body.FromJson<JoinGameReq>();
        sessions.Bind(message.Envelope.ActorId, context.SourceSessionNodeRid);
        var result = games.Join(
            request.GameId,
            message.Envelope.ActorId);

        await SendAllAsync(context, result.PlayerJoinedNotifications, SampleNames.PlayerJoinedPacket, cancellationToken)
            .ConfigureAwait(false);
        await SendAllAsync(context, result.StateNotifications, SampleNames.GameStatePacket, cancellationToken)
            .ConfigureAwait(false);

        return result.Reply.ToJson();
    }

    private async ValueTask<Message> HandlePlaceMarkAsync(
        IZLinkSessionProxyContext context,
        ZLinkActorRelayMessage message,
        CancellationToken cancellationToken)
    {
        var request = message.Body.FromJson<PlaceMarkReq>();
        var result = games.PlaceMark(
            request.GameId,
            message.Envelope.ActorId,
            request.Cell);

        await SendAllAsync(context, result.StateNotifications, SampleNames.GameStatePacket, cancellationToken)
            .ConfigureAwait(false);

        return result.Reply.ToJson();
    }

    private async ValueTask SendAllAsync<TMessage>(
        IZLinkSessionProxyContext context,
        IReadOnlyList<GameNotification<TMessage>> notifications,
        string packetName,
        CancellationToken cancellationToken)
    {
        foreach (var notification in notifications)
        {
            await context.SessionGateway
                .SendToActor(
                    SampleNames.RouterChannel,
                    sessions.GetSessionNodeRid(notification.ActorId),
                    notification.ActorId,
                    notification.Message)
                .WithPacketName(packetName)
                .Async(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}
