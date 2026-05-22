using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Server.Session.Sessions;

internal interface IBingoSessionHandler
{
    string PacketName { get; }

    ValueTask HandleAsync(
        BingoSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken);
}

internal readonly record struct BingoSessionContext(
    IZLinkSessionContext Stream,
    SessionRelayState State);
