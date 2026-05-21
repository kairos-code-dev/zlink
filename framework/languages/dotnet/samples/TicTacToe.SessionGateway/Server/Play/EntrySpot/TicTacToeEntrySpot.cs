using TicTacToe.SessionGateway.Play.EntrySpot.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play.EntrySpot;

internal sealed class TicTacToeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddHandler<JoinMatchHandler>();
        Context.AddHandler<TicTacToeEntrySpotActorJoinedHandler>();
        Context.AddHandler<TicTacToeEntrySpotActorLeftHandler>();
    }
}
