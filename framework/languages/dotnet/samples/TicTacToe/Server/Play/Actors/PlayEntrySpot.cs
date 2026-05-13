using Zlink.Framework.Spots;
using TicTacToe.Server.Play.Actors.Handlers;

namespace TicTacToe.Server.Play.Actors;

internal sealed class PlayEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddActorPacket<PlayActorJoinGameHandler, PlayActor>();
    }
}
