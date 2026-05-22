using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session.Sessions.Handlers;

internal sealed class MatchBingoBingoSessionHandler : IBingoSessionHandler
{
    public string PacketName => nameof(MatchBingoReq);

    public async ValueTask HandleAsync(
        BingoSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var actor = await context.State.RequireActorAsync(
                context.Stream,
                "matching bingo",
                cancellationToken)
            ;
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            ;
    }
}
