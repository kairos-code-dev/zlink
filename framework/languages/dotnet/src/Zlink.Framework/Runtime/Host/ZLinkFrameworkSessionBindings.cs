namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkSessionBindings
{
    private readonly ZLinkSessionActorBindingTable _sessionActorBindings = new();

    public void Bind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkSessionActor actorRef)
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

    public bool TryGetByActorId(
        string actorId,
        out ZLinkSessionContext context)
    {
        return _sessionActorBindings.TryGetByActorId(actorId, out context);
    }
}