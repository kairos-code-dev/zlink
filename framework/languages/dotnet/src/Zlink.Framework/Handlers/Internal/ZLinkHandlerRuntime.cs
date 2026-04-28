using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;

namespace Zlink.Framework.Handlers.Internal;

internal sealed record ZLinkHandlerEndpointDescriptor(
    ZLinkMessageKind Kind,
    string PacketName,
    Type DeclaringType,
    MethodInfo Method,
    Type MessageType,
    Type? ReplyType,
    Type? ContextType,
    bool HasCancellationToken);

internal sealed class ZLinkHandlerRegistry
{
    private readonly IReadOnlyDictionary<string, ZLinkHandlerEndpointDescriptor> _requests;
    private readonly IReadOnlyDictionary<string, ZLinkHandlerEndpointDescriptor> _commands;
    private readonly IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _events;

    public ZLinkHandlerRegistry(IEnumerable<ZLinkHandlerEndpointDescriptor> endpoints)
    {
        var requests = new Dictionary<string, ZLinkHandlerEndpointDescriptor>(StringComparer.Ordinal);
        var commands = new Dictionary<string, ZLinkHandlerEndpointDescriptor>(StringComparer.Ordinal);
        var events = new Dictionary<string, List<ZLinkHandlerEndpointDescriptor>>(StringComparer.Ordinal);

        foreach (var endpoint in endpoints)
        {
            switch (endpoint.Kind)
            {
                case ZLinkMessageKind.Request:
                    if (!requests.TryAdd(endpoint.PacketName, endpoint))
                    {
                        throw new ZLinkConfigurationException(
                            $"Duplicate request handler packet '{endpoint.PacketName}'.");
                    }

                    break;
                case ZLinkMessageKind.Command:
                    if (!commands.TryAdd(endpoint.PacketName, endpoint))
                    {
                        throw new ZLinkConfigurationException(
                            $"Duplicate send handler packet '{endpoint.PacketName}'.");
                    }

                    break;
                case ZLinkMessageKind.Event:
                    if (!events.TryGetValue(endpoint.PacketName, out var list))
                    {
                        list = [];
                        events.Add(endpoint.PacketName, list);
                    }

                    list.Add(endpoint);
                    break;
            }
        }

        _requests = requests;
        _commands = commands;
        _events = events.ToDictionary(
            entry => entry.Key,
            entry => (IReadOnlyList<ZLinkHandlerEndpointDescriptor>)entry.Value,
            StringComparer.Ordinal);
    }

    public ZLinkHandlerEndpointDescriptor GetRequest(string packetName)
    {
        return _requests.TryGetValue(packetName, out var endpoint)
            ? endpoint
            : throw new InvalidOperationException($"No request handler is registered for '{packetName}'.");
    }

    public ZLinkHandlerEndpointDescriptor GetCommand(string packetName)
    {
        return _commands.TryGetValue(packetName, out var endpoint)
            ? endpoint
            : throw new InvalidOperationException($"No send handler is registered for '{packetName}'.");
    }

    public IReadOnlyList<ZLinkHandlerEndpointDescriptor> GetEvents(string packetName)
    {
        return _events.TryGetValue(packetName, out var endpoints)
            ? endpoints
            : Array.Empty<ZLinkHandlerEndpointDescriptor>();
    }
}

internal sealed class ZLinkHandlerDispatcher(
    IServiceScopeFactory scopeFactory,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask<object?> DispatchAsync(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken)
    {
        await using var scope = scopeFactory.CreateAsyncScope();
        var scopedContext = RebindContext(context, scope.ServiceProvider);
        var filterTypes = registration.Filters;
        var invocation = new ZLinkHandlerInvocation(
            message,
            scopedContext,
            scopedContext.ChannelName,
            scopedContext.PacketName,
            scope.ServiceProvider);

        async ValueTask<object?> ExecuteHandler(CancellationToken ct)
        {
            var handler = scope.ServiceProvider.GetRequiredService(endpoint.DeclaringType);
            var args = BuildArguments(endpoint, message, scopedContext, ct);
            var result = endpoint.Method.Invoke(handler, args);
            return await ZLinkHandlerResultAwaiter.AwaitAsync(result);
        }

        ZLinkHandlerDelegate pipeline = ExecuteHandler;

        for (var index = filterTypes.Count - 1; index >= 0; index--)
        {
            var next = pipeline;
            var filterType = filterTypes[index];
            pipeline = async ct =>
            {
                var filter = (IZLinkHandlerFilter)scope.ServiceProvider.GetRequiredService(filterType);
                return await filter.InvokeAsync(invocation, next, ct);
            };
        }

        return await pipeline(cancellationToken);
    }

    private static object?[] BuildArguments(
        ZLinkHandlerEndpointDescriptor endpoint,
        object? message,
        ZLinkHandlerContext context,
        CancellationToken cancellationToken)
    {
        var parameters = endpoint.Method.GetParameters();
        var args = new object?[parameters.Length];

        if (parameters.Length > 0)
        {
            args[0] = message;
        }

        for (var i = 1; i < parameters.Length; i++)
        {
            if (typeof(CancellationToken) == parameters[i].ParameterType)
            {
                args[i] = cancellationToken;
                continue;
            }

            if (endpoint.ContextType is not null && parameters[i].ParameterType.IsAssignableFrom(endpoint.ContextType))
            {
                args[i] = context;
            }
        }

        return args;
    }

    private static ZLinkHandlerContext RebindContext(ZLinkHandlerContext context, IServiceProvider services)
    {
        return context switch
        {
            ZLinkRequestContext request => new ZLinkRequestContext(
                request.ChannelName,
                request.PacketName,
                request.ContentType,
                request.CorrelationId,
                request.Deadline,
                services,
                request.ConnectionAborted),
            ZLinkSendContext send => new ZLinkSendContext(
                send.ChannelName,
                send.PacketName,
                send.ContentType,
                send.CorrelationId,
                send.Deadline,
                services,
                send.ConnectionAborted),
            ZLinkEventContext @event => new ZLinkEventContext(
                @event.ChannelName,
                @event.PacketName,
                @event.ContentType,
                @event.CorrelationId,
                @event.Deadline,
                @event.Topic,
                @event.Source,
                services,
                @event.ConnectionAborted),
            _ => throw new InvalidOperationException("Unknown handler context type."),
        };
    }
}
