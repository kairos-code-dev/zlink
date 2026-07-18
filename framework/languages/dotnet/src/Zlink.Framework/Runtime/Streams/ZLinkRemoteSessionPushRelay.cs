using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.Runtime.Streams;

// Cross-node bound-session push relay (spec 31 §6: the session route is
// framework-internal state). Core's bound-session send only reaches sessions
// whose STREAM service lives on the same MeshNode, so after a cross-node actor
// join or transfer an actor push must travel back to the session's node. The
// actor's node wraps the encoded stream frame in this internal node-addressed
// route packet; the session node writes it to the still-bound local session as
// raw stream bytes — the same bytes a local push produces.
internal static class ZLinkRemoteSessionPushProtocol
{
    public const string PacketName = "$zlink.session.push-relay.v1";
}

// Session-node → actor-node direction: a session frame whose bound actor
// migrated to another node is relayed to the actor's owner node and dispatched
// there through the standard actor inbound pipeline (the reply travels back on
// the bound-session push relay above).
internal static class ZLinkRemoteActorFrameProtocol
{
    public const string PacketName = "$zlink.actor.frame-relay.v1";
}

internal sealed record ZLinkRemoteActorFrameRelay(
    string ActorId,
    ulong ActorGeneration,
    string SourceNodeRid,
    string SourceSessionRid,
    byte[] Header,
    byte[] Body);

internal sealed class ZLinkRemoteActorFrameRelayHandler(ZLinkFrameworkRuntime runtime)
    : IZLinkRouteSendHandler<ZLinkRemoteActorFrameRelay>
{
    public async ValueTask HandleAsync(
        ZLinkRemoteActorFrameRelay message,
        ZLinkRouteSendContext context,
        CancellationToken cancellationToken)
    {
        await runtime.DispatchRemoteActorFrameAsync(
                message.ActorId,
                message.ActorGeneration,
                RoutingId.FromHex(message.SourceNodeRid),
                RoutingId.FromHex(message.SourceSessionRid),
                message.Header,
                message.Body,
                cancellationToken)
            .ConfigureAwait(false);
    }
}

internal sealed record ZLinkRemoteSessionPushRelay(
    string ActorId,
    string SessionRid,
    byte[] Frame);

internal sealed class ZLinkRemoteSessionPushRelayHandler(ZLinkFrameworkRuntime runtime)
    : IZLinkRouteSendHandler<ZLinkRemoteSessionPushRelay>
{
    public ValueTask HandleAsync(
        ZLinkRemoteSessionPushRelay message,
        ZLinkRouteSendContext context,
        CancellationToken cancellationToken)
    {
        // A miss is a stale push racing a rebind or disconnect; spec 31 §6
        // forbids applying late pushes to a new binding, so it is dropped.
        runtime.DeliverRemoteSessionPush(
            message.ActorId,
            RoutingId.FromHex(message.SessionRid),
            message.Frame);
        return ValueTask.CompletedTask;
    }
}
