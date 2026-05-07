using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal interface ISessionRelayPacketHandler
{
    string PacketName { get; }

    ValueTask HandleAsync(
        SessionRelayPacketContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken);
}

internal readonly record struct SessionRelayPacketContext(
    IZLinkSessionContext Stream,
    SessionRelayState State);
