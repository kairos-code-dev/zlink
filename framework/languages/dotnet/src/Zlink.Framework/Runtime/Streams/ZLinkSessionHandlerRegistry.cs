using System.Reflection;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionHandlerRegistry(IServiceProvider services)
    : IZLinkSessionHandlerRegistry
{
    private readonly Dictionary<string, ZLinkSessionPacketHandlerDescriptor> _handlers =
        new(StringComparer.Ordinal);

    private bool _bound;
    private IZLinkSessionContext? _context;

    public void AddHandler<THandler>()
        where THandler : class
    {
        AddHandler(typeof(THandler), null);
    }

    public void AddHandler<THandler>(string packetName)
        where THandler : class
    {
        if (string.IsNullOrWhiteSpace(packetName)
            || !string.Equals(packetName, packetName.Trim(), StringComparison.Ordinal))
            throw new ZLinkConfigurationException(
                $"Session packet handler '{typeof(THandler).FullName}' must declare a non-empty packet name.");

        AddHandler(typeof(THandler), packetName);
    }

    public async ValueTask<bool> TryHandleAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default)
    {
        if (!_handlers.TryGetValue(dispatch.PacketName, out var handler)) return false;

        var context = _context
                      ?? throw new InvalidOperationException("Session handler registry is not bound to a session.");

        await handler.HandleAsync(services, context, dispatch, payload, cancellationToken)
            .ConfigureAwait(false);
        return true;
    }

    internal void BindContext(IZLinkSessionContext context)
    {
        if (_context is not null) throw new InvalidOperationException("Session handler registry is already bound.");

        _context = context;
    }

    internal void Bind()
    {
        _bound = true;
    }

    internal void AddScannedHandlers(IEnumerable<Assembly> assemblies)
    {
        EnsureNotBound();
        var context = _context
                      ?? throw new InvalidOperationException("Session handler registry is not bound to a session.");
        var contextType = context.GetType();
        foreach (var assembly in assemblies)
        foreach (var handlerType in assembly.GetTypes())
        {
            if (handlerType.IsAbstract || handlerType.IsInterface) continue;

            if (!ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType)
                    .Any(contract => contract.Definition == typeof(IZLinkSessionPacketHandler<,>)
                                     && contract.Arguments[0].IsAssignableFrom(contextType)))
                continue;

            AddHandler(handlerType, null);
        }
    }

    private void AddHandler(Type handlerType, string? packetName)
    {
        EnsureNotBound();
        var context = _context
                      ?? throw new InvalidOperationException("Session handler registry is not bound to a session.");
        var descriptor = ZLinkSessionPacketHandlerDescriptorFactory.Create(
            handlerType,
            context.GetType(),
            packetName);

        if (_handlers.TryGetValue(descriptor.PacketName, out var existing)
            && existing.HandlerType == descriptor.HandlerType
            && existing.MessageType == descriptor.MessageType)
            return;

        if (existing is not null)
            throw new ZLinkConfigurationException(
                $"Session packet handler '{descriptor.PacketName}' is already registered.");

        _handlers.Add(descriptor.PacketName, descriptor);
    }

    private void EnsureNotBound()
    {
        if (_bound)
            throw new InvalidOperationException(
                "Session handler registration is only allowed while Configure is running.");
    }
}

internal abstract class ZLinkSessionPacketHandlerDescriptor(
    Type handlerType,
    Type messageType,
    string packetName)
{
    public Type HandlerType { get; } = handlerType;

    public Type MessageType { get; } = messageType;

    public string PacketName { get; } = packetName;

    public abstract ValueTask HandleAsync(
        IServiceProvider services,
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken);
}

internal sealed class ZLinkSessionPacketHandlerDescriptor<TSessionContext, TMessage, THandler>
    : ZLinkSessionPacketHandlerDescriptor
    where THandler : class, IZLinkSessionPacketHandler<TSessionContext, TMessage>
{
    public ZLinkSessionPacketHandlerDescriptor(string packetName)
        : base(typeof(THandler), typeof(TMessage), packetName)
    {
    }

    public override async ValueTask HandleAsync(
        IServiceProvider services,
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        var handler = ActivatorUtilities.GetServiceOrCreateInstance<THandler>(services);
        await handler.HandleAsync(
                (TSessionContext)context,
                dispatch,
                payload.Decode<TMessage>(),
                cancellationToken)
            .ConfigureAwait(false);
    }
}

internal static class ZLinkSessionPacketHandlerDescriptorFactory
{
    public static ZLinkSessionPacketHandlerDescriptor Create(
        Type handlerType,
        Type sessionContextType,
        string? packetName)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkSessionPacketHandler<,>)) continue;

            var handlerContextType = arguments[0];
            if (!handlerContextType.IsAssignableFrom(sessionContextType))
                throw new ZLinkConfigurationException(
                    $"Session packet handler '{handlerType.FullName}' targets context '{handlerContextType.FullName}', but the session context is '{sessionContextType.FullName}'.");

            var messageType = arguments[1];
            return (ZLinkSessionPacketHandlerDescriptor)Activator.CreateInstance(
                typeof(ZLinkSessionPacketHandlerDescriptor<,,>).MakeGenericType(
                    handlerContextType,
                    messageType,
                    handlerType),
                packetName ?? ZLinkMessageNameResolver.ResolveFromType(messageType))!;
        }

        throw new ZLinkConfigurationException(
            $"Session packet handler '{handlerType.FullName}' must implement IZLinkSessionPacketHandler<TSessionContext, TMessage>.");
    }
}
