using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;

namespace Zlink.Framework;

internal enum ZLinkMessageKind
{
    Request = 1,
    Response = 2,
    Command = 3,
    Event = 4,
    Error = 5,
}

internal sealed record ZLinkEnvelopeHeader(
    ZLinkMessageKind Kind,
    string ChannelName,
    string PacketName,
    string ContentType,
    string? CorrelationId,
    DateTimeOffset? Deadline,
    string? Topic,
    string? ErrorCode,
    string? ErrorMessage);

internal static class ZLinkEnvelopeCodec
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private const string JsonContentType = "application/json";

    public static global::Zlink.Message Encode(
        ZLinkEnvelopeHeader header,
        object? body,
        Type? bodyType)
    {
        var envelope = new ZLinkSerializedEnvelope(
            header,
            bodyType is not null && body is not null
                ? JsonSerializer.SerializeToElement(body, bodyType, JsonOptions)
                : null);
        return global::Zlink.Message.FromString(
            JsonSerializer.Serialize(envelope, JsonOptions));
    }

    public static ZLinkEnvelopeHeader DecodeHeader(global::Zlink.Message message)
    {
        return DecodeEnvelope(message).Header;
    }

    public static object? DecodeBody(global::Zlink.Message message, Type bodyType)
    {
        var envelope = DecodeEnvelope(message);
        if (envelope.Body is null)
        {
            return bodyType.IsValueType
                ? Activator.CreateInstance(bodyType)
                : null;
        }

        return envelope.Body.Value.Deserialize(bodyType, JsonOptions);
    }

    private static ZLinkSerializedEnvelope DecodeEnvelope(global::Zlink.Message message)
    {
        var envelope = JsonSerializer.Deserialize<ZLinkSerializedEnvelope>(
            message.AsReadOnlySpan(),
            JsonOptions);

        return envelope ?? throw new InvalidOperationException("Invalid ZLink envelope header.");
    }

    public static string DefaultContentType => JsonContentType;

    private sealed record ZLinkSerializedEnvelope(
        ZLinkEnvelopeHeader Header,
        JsonElement? Body);
}

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
            return await AwaitResultAsync(result);
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

    private static async ValueTask<object?> AwaitResultAsync(object? result)
    {
        if (result is null)
        {
            return null;
        }

        switch (result)
        {
            case Task task when result.GetType() == typeof(Task):
                await task.ConfigureAwait(false);
                return null;
            case ValueTask valueTask:
                await valueTask.ConfigureAwait(false);
                return null;
        }

        var resultType = result.GetType();
        if (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(Task<>))
        {
            var task = (Task)result;
            await task.ConfigureAwait(false);
            return resultType.GetProperty("Result")!.GetValue(result);
        }

        if (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(ValueTask<>))
        {
            var asTask = (Task)resultType.GetMethod("AsTask")!.Invoke(result, null)!;
            await asTask.ConfigureAwait(false);
            return asTask.GetType().GetProperty("Result")!.GetValue(asTask);
        }

        return result;
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

internal sealed class ZLinkChannelRuntimeBundle : IAsyncDisposable
{
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);

    public ZLinkChannelRuntimeBundle(IZLinkBackendSocket socket)
    {
        Socket = socket;
    }

    public IZLinkBackendSocket Socket { get; }

    public IZLinkBackendDiscovery? Discovery { get; set; }

    public bool TryAddManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Add(endpoint);
        }
    }

    public void RemoveManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            _manualConnections.Remove(endpoint);
        }
    }

    public bool ContainsManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Contains(endpoint);
        }
    }

    public IReadOnlyList<string> ListManualConnections()
    {
        lock (_manualConnections)
        {
            return _manualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Discovery is not null)
        {
            await Discovery.DisposeAsync();
        }

        await Socket.DisposeAsync();
    }
}

internal sealed class ZLinkChannelConnectionManager(ZLinkFrameworkRuntime runtime) : IZLinkChannelConnectionManager
{
    public ValueTask<IZLinkEndpointConnections> GetClientAsync(
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetClientConnectionsAsync(channelName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSubscriberConnectionsAsync(channelName, cancellationToken);
    }
}

internal sealed class ZLinkRuntimeConnections(
    Func<string, CancellationToken, ValueTask<bool>> connect,
    Func<string, CancellationToken, ValueTask> disconnect,
    Func<CancellationToken, ValueTask<IReadOnlyList<string>>> list)
    : IZLinkEndpointConnections
{
    public ValueTask<bool> ConnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default)
    {
        return connect(endpoint, cancellationToken);
    }

    public ValueTask DisconnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default)
    {
        return disconnect(endpoint, cancellationToken);
    }

    public ValueTask<IReadOnlyList<string>> ListConnectionsAsync(
        CancellationToken cancellationToken = default)
    {
        return list(cancellationToken);
    }
}

internal sealed class ZLinkClient(ZLinkFrameworkRuntime runtime, ZLinkFrameworkRegistration registration) : IZLinkClient
{
    public IZLinkSendCall Send<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkSendCall(runtime, registration, channelName, message);
    }

    public IZLinkRequestCall<TReply> Request<TReply>(string channelName, IZLinkRequest<TReply> request)
    {
        return new ZLinkRequestCall<TReply>(runtime, registration, channelName, request);
    }
}

internal sealed class ZLinkEventPublisher(ZLinkFrameworkRuntime runtime) : IZLinkEventPublisher
{
    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkPublishCall(runtime, channelName, topic, message);
    }
}

internal sealed class ZLinkSendCall : IZLinkSendCall
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly string _channelName;
    private readonly object? _message;
    private string? _packetName;
    private global::Zlink.SendFlags _flags;

    public ZLinkSendCall(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration,
        string channelName,
        object? message)
    {
        _runtime = runtime;
        _channelName = channelName;
        _message = message;
        _packetName = ZLinkPacketNameResolver.ResolveFromMessage(message);
        _ = registration;
    }

    public IZLinkSendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSendCall WithDontWait()
    {
        _flags |= global::Zlink.SendFlags.DontWait;
        return this;
    }

    public bool Exec()
    {
        var bundle = _runtime.GetOrCreateClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            _channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);

        using var message = ZLinkEnvelopeCodec.Encode(header, _message, _message?.GetType());
        return dealer.Send(message, _flags);
    }
}

internal sealed class ZLinkRequestCall<TReply> : IZLinkRequestCall<TReply>
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly string _channelName;
    private readonly IZLinkRequest<TReply> _request;
    private string? _packetName;
    private TimeSpan? _timeout;

    public ZLinkRequestCall(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration,
        string channelName,
        IZLinkRequest<TReply> request)
    {
        _runtime = runtime;
        _registration = registration;
        _channelName = channelName;
        _request = request;
        _packetName = ZLinkPacketNameResolver.ResolveFromMessage(request);
    }

    public IZLinkRequestCall<TReply> WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkRequestCall<TReply> WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> ExecAsync(CancellationToken cancellationToken = default)
    {
        var bundle = _runtime.GetOrCreateClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var correlationId = Guid.NewGuid().ToString("N");
        var timeout = _timeout ?? _registration.DefaultTimeout;
        var deadline = DateTimeOffset.UtcNow.Add(timeout);
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            _channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId,
            deadline,
            null,
            null,
            null);
        using var message = ZLinkEnvelopeCodec.Encode(header, _request, _request.GetType());
        var reply = await dealer.RequestAsync(message, timeout, cancellationToken).ConfigureAwait(false);
        try
        {
            if (reply.Count == 0)
            {
                throw new InvalidOperationException("ZLink request reply is empty.");
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply[0]);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                throw new InvalidOperationException(replyHeader.ErrorMessage ?? "ZLink request failed.");
            }

            return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply[0], typeof(TReply))
                ?? throw new InvalidOperationException("ZLink request reply body is null.");
        }
        finally
        {
            foreach (var replyPart in reply)
            {
                replyPart.Dispose();
            }
        }
    }
}

internal sealed class ZLinkPublishCall : IZLinkPublishCall
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly string _channelName;
    private readonly string _topic;
    private readonly object? _message;
    private string? _packetName;
    private global::Zlink.SendFlags _flags;

    public ZLinkPublishCall(
        ZLinkFrameworkRuntime runtime,
        string channelName,
        string topic,
        object? message)
    {
        _runtime = runtime;
        _channelName = channelName;
        _topic = topic;
        _message = message;
        _packetName = ZLinkPacketNameResolver.ResolveFromMessage(message);
    }

    public IZLinkPublishCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkPublishCall WithDontWait()
    {
        _flags |= global::Zlink.SendFlags.DontWait;
        return this;
    }

    public bool Exec()
    {
        var bundle = _runtime.GetOrCreatePublisherBundle(_channelName);
        var publisher = (IZLinkBackendPublisherSocket)bundle.Socket;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Event,
            _channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            _topic,
            null,
            null);
        using var message = ZLinkEnvelopeCodec.Encode(header, _message, _message?.GetType());
        return publisher.Publish(_topic, message, _flags);
    }
}

internal static class ZLinkPacketNameResolver
{
    public static string ResolveFromMessage(object? message)
    {
        var messageType = message?.GetType()
            ?? throw new InvalidOperationException("Message type is required.");
        return ResolveFromType(messageType);
    }

    public static string ResolveFromType(Type type)
    {
        return type.GetCustomAttribute<ZLinkPacketAttribute>()?.PacketName
            ?? type.Name;
    }
}

internal static class ZLinkHandlerScanner
{
    public static IReadOnlyList<ZLinkHandlerEndpointDescriptor> Scan(Assembly assembly)
    {
        var endpoints = new List<ZLinkHandlerEndpointDescriptor>();

        foreach (var type in assembly.GetTypes())
        {
            foreach (var method in type.GetMethods(BindingFlags.Instance | BindingFlags.Public))
            {
                var request = method.GetCustomAttribute<ZLinkRequestAttribute>();
                if (request is not null)
                {
                    endpoints.Add(CreateDescriptor(type, method, request.PacketName, ZLinkMessageKind.Request));
                }

                var send = method.GetCustomAttribute<ZLinkSendAttribute>();
                if (send is not null)
                {
                    endpoints.Add(CreateDescriptor(type, method, send.PacketName, ZLinkMessageKind.Command));
                }

                var @event = method.GetCustomAttribute<ZLinkEventAttribute>();
                if (@event is not null)
                {
                    endpoints.Add(CreateDescriptor(type, method, @event.PacketName, ZLinkMessageKind.Event));
                }
            }
        }

        return endpoints;
    }

    private static ZLinkHandlerEndpointDescriptor CreateDescriptor(
        Type declaringType,
        MethodInfo method,
        string? packetNameOverride,
        ZLinkMessageKind kind)
    {
        var parameters = method.GetParameters();
        if (parameters.Length == 0)
        {
            throw new ZLinkConfigurationException(
                $"Handler method '{declaringType.FullName}.{method.Name}' must accept a message parameter.");
        }

        var messageType = parameters[0].ParameterType;
        var packetName = packetNameOverride ?? ZLinkPacketNameResolver.ResolveFromType(messageType);
        Type? contextType = null;
        var hasCancellationToken = false;

        for (var i = 1; i < parameters.Length; i++)
        {
            if (parameters[i].ParameterType == typeof(CancellationToken))
            {
                hasCancellationToken = true;
                continue;
            }

            if (typeof(ZLinkHandlerContext).IsAssignableFrom(parameters[i].ParameterType))
            {
                contextType = parameters[i].ParameterType;
            }
        }

        var replyType = kind == ZLinkMessageKind.Request
            ? GetReplyType(method.ReturnType)
            : null;

        return new ZLinkHandlerEndpointDescriptor(
            kind,
            packetName,
            declaringType,
            method,
            messageType,
            replyType,
            contextType,
            hasCancellationToken);
    }

    private static Type? GetReplyType(Type returnType)
    {
        if (returnType.IsGenericType && returnType.GetGenericTypeDefinition() == typeof(ValueTask<>))
        {
            return returnType.GetGenericArguments()[0];
        }

        if (returnType.IsGenericType && returnType.GetGenericTypeDefinition() == typeof(Task<>))
        {
            return returnType.GetGenericArguments()[0];
        }

        if (returnType == typeof(ValueTask) || returnType == typeof(Task))
        {
            throw new ZLinkConfigurationException("Request handlers must return a reply value.");
        }

        return returnType;
    }
}
