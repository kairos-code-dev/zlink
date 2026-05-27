using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Play.EntrySpot.Handlers;

namespace TicTacToe.Server.Play.EntrySpot;

internal sealed class PlayEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<PlayActorJoinGameHandler>();
        Context.Handlers.AddHandler<PlayEntrySpotActorJoinedHandler>();
        Context.Handlers.AddHandler<PlayEntrySpotActorLeftHandler>();
    }
}
