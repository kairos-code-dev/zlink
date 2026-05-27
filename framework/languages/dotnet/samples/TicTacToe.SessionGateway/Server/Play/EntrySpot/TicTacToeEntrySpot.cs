using TicTacToe.SessionGateway.Play.EntrySpot.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Server.Play.EntrySpot;

internal sealed class TicTacToeEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<JoinMatchHandler>();
        Context.Handlers.AddHandler<TicTacToeEntrySpotActorJoinedHandler>();
        Context.Handlers.AddHandler<TicTacToeEntrySpotActorLeftHandler>();
    }
}
