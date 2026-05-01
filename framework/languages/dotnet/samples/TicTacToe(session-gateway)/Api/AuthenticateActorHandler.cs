using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Configuration;
using Zlink.Framework.Handlers;

namespace TicTacToe.SessionActorDispatch.Api;

internal sealed class AuthenticateActorHandler
{
    [ZLinkRequest]
    public AuthenticateActorRes AuthenticateActor(
        AuthenticateActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();

        var actorId = request.ActorId.Trim();
        if (string.IsNullOrWhiteSpace(actorId))
        {
            return new AuthenticateActorRes(false, null, "Actor id must not be empty.");
        }

        if (!string.Equals(actorId, SampleNames.XActorId, StringComparison.Ordinal)
            && !string.Equals(actorId, SampleNames.OActorId, StringComparison.Ordinal))
        {
            return new AuthenticateActorRes(false, null, $"Actor '{actorId}' is not registered in this sample.");
        }

        return new AuthenticateActorRes(true, actorId, null);
    }
}
