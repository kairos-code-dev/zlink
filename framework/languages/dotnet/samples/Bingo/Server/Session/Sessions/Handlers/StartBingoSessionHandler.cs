using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session.Sessions.Handlers;

internal sealed class StartBingoBingoSessionHandler : IBingoSessionHandler
{
    public string PacketName => nameof(StartBingoGameReq);

    public async ValueTask HandleAsync(
        BingoSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var actor = await context.State.RequireActorAsync(
                context.Stream,
                "starting bingo",
                cancellationToken)
            ;
        await context.Stream.RelayToActorAsync(actor, header, payload, cancellationToken)
            ;
    }
}
