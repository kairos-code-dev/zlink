namespace Zlink.Framework.Contracts.Handlers;

public sealed class ZLinkHandlerInvocation
{
    internal ZLinkHandlerInvocation(
        string ownerKind,
        IZLinkMessageContext messageContext)
    {
        OwnerKind = ownerKind;
        MessageContext = messageContext;
    }

    public string OwnerKind { get; }

    public IZLinkMessageContext MessageContext { get; }
}
