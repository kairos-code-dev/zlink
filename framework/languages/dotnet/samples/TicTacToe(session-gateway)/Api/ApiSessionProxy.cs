using TicTacToe.SessionGateway.Contracts;
using Zlink;
using Zlink.Codecs.Json;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionGateway.Api;

internal sealed class ApiSessionProxy(GameLocationStore locations) : IZLinkSessionProxyHandler
{
    public ValueTask<Message?> HandleAsync(
        IZLinkSessionProxyContext context,
        ZLinkActorRelayMessage message,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;
        return message.Envelope.StreamHeader.Name switch
        {
            nameof(CreateGameReq) => new ValueTask<Message?>(
                locations.Create(message.Body.FromJson<CreateGameReq>().PreferredGameId).ToJson()),
            nameof(ResolveGameReq) => new ValueTask<Message?>(
                locations.Resolve(message.Body.FromJson<ResolveGameReq>().GameId).ToJson()),
            _ => throw new InvalidOperationException(
                $"Unsupported API packet '{message.Envelope.StreamHeader.Name}'.")
        };
    }
}
