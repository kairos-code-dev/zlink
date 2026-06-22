using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using System.Text;
using System.Text.Json;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.Session;

internal sealed class CustomerSession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers,
    CustomerSessionDirectory sessions) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        sessions.Add(Context);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        sessions.Remove(Context);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = error;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (await handlers.TryHandleAsync(Context, header, payload, cancellationToken))
        {
            return;
        }

        var actor = Context.Actors.Bound.Single();
        await actor.RelayAsync(header, payload, cancellationToken);
    }
}

internal sealed class SubscribeDeliveryHandler(
    IZLinkChannelClient channels,
    CustomerSessionDirectory sessions)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    private const string CustomerId = "customer-1";

    public string PacketName => nameof(SubscribeDelivery);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var raw = Encoding.UTF8.GetString(payload.AsReadOnlySpan());
        var deliveryId = ReadDeliveryId(raw);
        var ensured = await channels.RequestToChannel(
                SampleNames.TrackingRouteChannel,
                new EnsureCustomerActor(CustomerId))
            .Async<CustomerActorEnsured>(cancellationToken);
        await context.Actors.BindAsync(
            new ActorRef(
                RoutingId.From(ensured.Actor.NodeRid),
                ensured.Actor.ActorId,
                ensured.Actor.Generation),
            cancellationToken);
        Console.Error.WriteLine($"deliverydispatch session: bound customer actor={ensured.Actor.ActorId}");

        var subscribed = await channels.RequestToChannel(
                SampleNames.TrackingRouteChannel,
                new SubscribeCustomerToDelivery(CustomerId, deliveryId))
            .Async<CustomerDeliverySubscribed>(cancellationToken);

        sessions.Subscribe(context, subscribed.DeliveryId);
        await context.Client.Reply(new SubscribeDeliveryAccepted(subscribed.DeliveryId))
            .Async();
    }

    private static string ReadDeliveryId(string raw)
    {
        using var document = JsonDocument.Parse(raw);
        if (document.RootElement.TryGetProperty("deliveryId", out var value)
            && !string.IsNullOrWhiteSpace(value.GetString()))
        {
            return value.GetString()!;
        }

        throw new InvalidOperationException("SubscribeDelivery requires deliveryId.");
    }
}
