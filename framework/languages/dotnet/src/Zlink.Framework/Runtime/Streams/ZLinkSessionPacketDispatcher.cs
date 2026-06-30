namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionPacketDispatcher<TSessionContext>(
    IEnumerable<IZLinkSessionPacketHandler<TSessionContext>> handlers)
    : IZLinkSessionPacketDispatcher<TSessionContext>
{
    private readonly IReadOnlyDictionary<string, IZLinkSessionPacketHandler<TSessionContext>> _handlers =
        BuildHandlerMap(handlers);

    public async ValueTask<bool> TryHandleAsync(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default)
    {
        if (!_handlers.TryGetValue(dispatch.PacketName, out var handler)) return false;

        await handler.HandleAsync(context, dispatch, payload, cancellationToken)
            .ConfigureAwait(false);
        return true;
    }

    private static IReadOnlyDictionary<string, IZLinkSessionPacketHandler<TSessionContext>> BuildHandlerMap(
        IEnumerable<IZLinkSessionPacketHandler<TSessionContext>> handlers)
    {
        var map = new Dictionary<string, IZLinkSessionPacketHandler<TSessionContext>>(StringComparer.Ordinal);
        foreach (var handler in handlers)
        {
            var packetName = handler.PacketName;
            if (string.IsNullOrWhiteSpace(packetName)
                || !string.Equals(packetName, packetName.Trim(), StringComparison.Ordinal))
                throw new ZLinkConfigurationException(
                    $"Session packet handler '{handler.GetType().FullName}' must declare a non-empty packet name.");

            if (!map.TryAdd(packetName, handler))
                throw new ZLinkConfigurationException(
                    $"Duplicate session packet handler '{packetName}' for context '{typeof(TSessionContext).FullName}'.");
        }

        return map;
    }
}