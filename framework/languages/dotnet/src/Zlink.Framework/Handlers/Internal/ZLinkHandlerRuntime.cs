using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Handlers.Internal;

internal enum ZLinkHandlerArgumentKind
{
    Message,
    Context,
    CancellationToken,
    Default
}

internal sealed record ZLinkHandlerEndpointDescriptor(
    ZLinkMessageKind Kind,
    string MessageName,
    Type DeclaringType,
    ZLinkHandlerMethodInvoker Invoker,
    IReadOnlyList<ZLinkHandlerArgumentKind> ArgumentPlan,
    Type MessageType,
    Type? ReplyType,
    Type? ContextType,
    bool HasCancellationToken,
    IReadOnlySet<string> Groups);

internal sealed class ZLinkHandlerRegistry
{
    private readonly IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _requests;
    private readonly IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _commands;
    private readonly IReadOnlyDictionary<string, IReadOnlyList<ZLinkHandlerEndpointDescriptor>> _publishes;

    public ZLinkHandlerRegistry(IEnumerable<ZLinkHandlerEndpointDescriptor> endpoints)
    {
        var requests = new Dictionary<string, List<ZLinkHandlerEndpointDescriptor>>(StringComparer.Ordinal);
        var commands = new Dictionary<string, List<ZLinkHandlerEndpointDescriptor>>(StringComparer.Ordinal);
        var publishes = new Dictionary<string, List<ZLinkHandlerEndpointDescriptor>>(StringComparer.Ordinal);

        foreach (var endpoint in endpoints)
        {
            switch (endpoint.Kind)
            {
                case ZLinkMessageKind.Request:
                    if (!requests.TryGetValue(endpoint.MessageName, out var requestList))
                    {
                        requestList = [];
                        requests.Add(endpoint.MessageName, requestList);
                    }

                    requestList.Add(endpoint);
                    break;
                case ZLinkMessageKind.Command:
                    if (!commands.TryGetValue(endpoint.MessageName, out var commandList))
                    {
                        commandList = [];
                        commands.Add(endpoint.MessageName, commandList);
                    }

                    commandList.Add(endpoint);
                    break;
                case ZLinkMessageKind.Publish:
                    if (!publishes.TryGetValue(endpoint.MessageName, out var list))
                    {
                        list = [];
                        publishes.Add(endpoint.MessageName, list);
                    }

                    list.Add(endpoint);
                    break;
            }
        }

        _requests = requests.ToDictionary(
            entry => entry.Key,
            entry => (IReadOnlyList<ZLinkHandlerEndpointDescriptor>)entry.Value,
            StringComparer.Ordinal);
        _commands = commands.ToDictionary(
            entry => entry.Key,
            entry => (IReadOnlyList<ZLinkHandlerEndpointDescriptor>)entry.Value,
            StringComparer.Ordinal);
        _publishes = publishes.ToDictionary(
            entry => entry.Key,
            entry => (IReadOnlyList<ZLinkHandlerEndpointDescriptor>)entry.Value,
            StringComparer.Ordinal);
    }

    public ZLinkHandlerEndpointDescriptor GetRequest(
        string channelName,
        IReadOnlySet<string> mappedGroups,
        string messageName)
    {
        return _requests.TryGetValue(messageName, out var endpoints)
            ? SelectEndpoint(channelName, mappedGroups, messageName, endpoints, "request")
            : throw new InvalidOperationException($"No request handler is registered for '{messageName}'.");
    }

    public ZLinkHandlerEndpointDescriptor GetCommand(
        string channelName,
        IReadOnlySet<string> mappedGroups,
        string messageName)
    {
        return _commands.TryGetValue(messageName, out var endpoints)
            ? SelectEndpoint(channelName, mappedGroups, messageName, endpoints, "send")
            : throw new InvalidOperationException($"No send handler is registered for '{messageName}'.");
    }

    public IReadOnlyList<ZLinkHandlerEndpointDescriptor> GetPublishes(
        IReadOnlySet<string> mappedGroups,
        string messageName)
    {
        return _publishes.TryGetValue(messageName, out var endpoints)
            ? FilterEndpoints(mappedGroups, endpoints)
            : Array.Empty<ZLinkHandlerEndpointDescriptor>();
    }

    private static ZLinkHandlerEndpointDescriptor SelectEndpoint(
        string channelName,
        IReadOnlySet<string> mappedGroups,
        string messageName,
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> endpoints,
        string kind)
    {
        var matches = FilterEndpoints(mappedGroups, endpoints);
        return matches.Count switch
        {
            1 => matches[0],
            0 => throw new InvalidOperationException(
                $"No {kind} handler is mapped for '{channelName}:{messageName}'."),
            _ => throw new ZLinkConfigurationException(
                $"Duplicate {kind} handler packet '{messageName}' is mapped to channel '{channelName}'.")
        };
    }

    private static IReadOnlyList<ZLinkHandlerEndpointDescriptor> FilterEndpoints(
        IReadOnlySet<string> mappedGroups,
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> endpoints)
    {
        if (mappedGroups.Count == 0)
        {
            return endpoints;
        }

        return endpoints
            .Where(endpoint => endpoint.Groups.Count > 0 && endpoint.Groups.Any(mappedGroups.Contains))
            .ToArray();
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
            var result = endpoint.Invoker(handler, args);
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
        var args = new object?[endpoint.ArgumentPlan.Count];

        for (var i = 0; i < endpoint.ArgumentPlan.Count; i++)
        {
            switch (endpoint.ArgumentPlan[i])
            {
                case ZLinkHandlerArgumentKind.Message:
                    args[i] = message;
                    break;
                case ZLinkHandlerArgumentKind.Context:
                    args[i] = context;
                    break;
                case ZLinkHandlerArgumentKind.CancellationToken:
                    args[i] = cancellationToken;
                    break;
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
            ZLinkPublishContext publish => new ZLinkPublishContext(
                publish.ChannelName,
                publish.PacketName,
                publish.ContentType,
                publish.CorrelationId,
                publish.Deadline,
                publish.Topic,
                publish.Source,
                services,
                publish.ConnectionAborted),
            _ => throw new InvalidOperationException("Unknown handler context type."),
        };
    }
}
