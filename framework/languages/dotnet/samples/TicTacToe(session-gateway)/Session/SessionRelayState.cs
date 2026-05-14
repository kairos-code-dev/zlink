using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class SessionRelayState
{
    public IZLinkActorRef? Actor { get; set; }

    public string? ActorId { get; set; }

    public ZLinkActorRoute? ActorRoute { get; set; }

    public string RequireActorId(string action)
    {
        return ActorId
            ?? throw new InvalidOperationException($"Client must authenticate before {action}.");
    }

    public void Clear()
    {
        Actor = null;
        ActorId = null;
        ActorRoute = null;
    }
}
