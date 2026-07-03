using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CourierSession;

internal sealed class BindCourierSessionHandler(
    CourierSessionBinder binder)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(BindCourierSessionReq);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var request = payload.Decode<BindCourierSessionReq>();
        var bound = await binder.BindAsync(request.CourierId, context, cancellationToken);
        context.Client.Reply(bound).Submit();
    }
}
