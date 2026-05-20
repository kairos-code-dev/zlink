namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkFrameworkSessionBindings
{
    private readonly ZLinkSessionActorBindingTable _sessionActorBindings = new();

    public void Bind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkActorRef actorRef)
    {
        _sessionActorBindings.Bind(actorId, context, bindingToken, actorRef);
    }

    public void Unbind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _sessionActorBindings.Unbind(actorId, context, bindingToken);
    }

    public bool TryGet(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        return _sessionActorBindings.TryGet(actorId, bindingToken, out context);
    }

    public int UpdateAttachedActorRoute(
        string actorId,
        string routerChannelId,
        RoutingId targetNodeRid,
        ulong expectedActorGeneration,
        ulong newActorGeneration)
    {
        return _sessionActorBindings.UpdateAttachedActorRoute(
            actorId,
            routerChannelId,
            targetNodeRid,
            expectedActorGeneration,
            newActorGeneration);
    }
}
