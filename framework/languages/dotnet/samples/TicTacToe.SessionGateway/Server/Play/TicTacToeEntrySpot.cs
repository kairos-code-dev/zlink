using TicTacToe.SessionActorDispatch.Play;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play;

internal sealed class TicTacToeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddActorPacket<JoinMatchHandler, PlayerActor>();
    }
}
