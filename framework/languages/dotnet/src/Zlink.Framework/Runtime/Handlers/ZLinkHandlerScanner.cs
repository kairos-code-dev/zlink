using System.Reflection;

namespace Zlink.Framework.Runtime.Handlers;

internal static class ZLinkHandlerScanner
{
    public static IReadOnlyList<ZLinkHandlerEndpointDescriptor> Scan(Assembly assembly)
    {
        var endpoints = new List<ZLinkHandlerEndpointDescriptor>();

        foreach (var type in assembly.GetTypes())
        {
            if (type.IsAbstract || type.IsInterface)
            {
                continue;
            }

            var groups = ResolveGroups(type);
            foreach (var method in type.GetMethods(BindingFlags.Instance | BindingFlags.Public))
            {
                foreach (var attribute in EnumerateEndpointAttributes(method))
                {
                    endpoints.Add(CreateDescriptor(
                        type,
                        method,
                        attribute.PacketName,
                        attribute.Kind,
                        groups));
                }
            }

            foreach (var iface in type.GetInterfaces())
            {
                if (!iface.IsGenericType)
                {
                    continue;
                }

                var def = iface.GetGenericTypeDefinition();
                if (def == typeof(IZLinkRequestHandler<,>))
                {
                    endpoints.Add(CreateInterfaceDescriptor(
                        type,
                        iface,
                        ZLinkMessageKind.Request,
                        groups,
                        null,
                        null));
                }
                else if (def == typeof(IZLinkSendHandler<>))
                {
                    endpoints.Add(CreateInterfaceDescriptor(
                        type,
                        iface,
                        ZLinkMessageKind.Command,
                        groups,
                        null,
                        null));
                }
                else if (def == typeof(IZLinkPublishHandler<>))
                {
                    endpoints.Add(CreateInterfaceDescriptor(
                        type,
                        iface,
                        ZLinkMessageKind.Publish,
                        groups,
                        null,
                        null));
                }
            }
        }

        return endpoints;
    }

    public static IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> ScanRoute(Assembly assembly)
    {
        var endpoints = new List<ZLinkRouteHandlerEndpointDescriptor>();

        foreach (var type in assembly.GetTypes())
        {
            if (type.IsAbstract || type.IsInterface)
            {
                continue;
            }

            var groups = ResolveGroups(type);
            foreach (var iface in type.GetInterfaces())
            {
                if (!iface.IsGenericType)
                {
                    continue;
                }

                var def = iface.GetGenericTypeDefinition();
                if (def == typeof(IZLinkRouteRequestHandler<,>))
                {
                    endpoints.Add(CreateRouteInterfaceDescriptor(
                        type,
                        iface,
                        ZLinkMessageKind.Request,
                        groups,
                        null));
                }
                else if (def == typeof(IZLinkRouteSendHandler<>))
                {
                    endpoints.Add(CreateRouteInterfaceDescriptor(
                        type,
                        iface,
                        ZLinkMessageKind.Command,
                        groups,
                        null));
                }
            }
        }

        return endpoints;
    }

    public static ZLinkHandlerEndpointDescriptor CreateExplicitInterfaceDescriptor(
        Type declaringType,
        Type handlerInterface,
        ZLinkMessageKind kind,
        string channelName,
        string? packetName)
    {
        return CreateInterfaceDescriptor(
            declaringType,
            handlerInterface,
            kind,
            new HashSet<string>(StringComparer.Ordinal),
            channelName,
            packetName);
    }

    public static ZLinkRouteHandlerEndpointDescriptor CreateExplicitRouteInterfaceDescriptor(
        Type declaringType,
        Type handlerInterface,
        ZLinkMessageKind kind,
        string? packetName)
    {
        return CreateRouteInterfaceDescriptor(
            declaringType,
            handlerInterface,
            kind,
            new HashSet<string>(StringComparer.Ordinal),
            packetName);
    }

    private static IEnumerable<ZLinkEndpointAttributeDescriptor> EnumerateEndpointAttributes(MethodInfo method)
    {
        if (method.GetCustomAttribute<ZLinkRequestAttribute>() is { } request)
        {
            yield return new ZLinkEndpointAttributeDescriptor(
                ZLinkMessageKind.Request,
                request.PacketName);
        }

        if (method.GetCustomAttribute<ZLinkSendAttribute>() is { } send)
        {
            yield return new ZLinkEndpointAttributeDescriptor(
                ZLinkMessageKind.Command,
                send.PacketName);
        }

        if (method.GetCustomAttribute<ZLinkPublishAttribute>() is { } publish)
        {
            yield return new ZLinkEndpointAttributeDescriptor(
                ZLinkMessageKind.Publish,
                publish.PacketName);
        }
    }

    private static ZLinkHandlerEndpointDescriptor CreateInterfaceDescriptor(
        Type declaringType,
        Type handlerInterface,
        ZLinkMessageKind kind,
        IReadOnlySet<string> groups,
        string? explicitChannelName,
        string? packetName)
    {
        var args = handlerInterface.GetGenericArguments();
        var messageType = args[0];
        var replyType = kind == ZLinkMessageKind.Request ? args[1] : null;

        var map = declaringType.GetInterfaceMap(handlerInterface);
        MethodInfo? targetMethod = null;
        for (var i = 0; i < map.InterfaceMethods.Length; i++)
        {
            if (map.InterfaceMethods[i].Name == nameof(IZLinkPublishHandler<object>.HandleAsync))
            {
                targetMethod = map.TargetMethods[i];
                break;
            }
        }

        if (targetMethod is null)
        {
            throw new ZLinkConfigurationException(
                $"Handler '{declaringType.FullName}' does not implement HandleAsync for '{handlerInterface.Name}'.");
        }

        var messageName = packetName ?? ZLinkMessageNameResolver.ResolveFromType(messageType);
        var contextType = kind switch
        {
            ZLinkMessageKind.Request => typeof(ZLinkRequestContext),
            ZLinkMessageKind.Command => typeof(ZLinkSendContext),
            _ => typeof(ZLinkPublishContext),
        };

        return new ZLinkHandlerEndpointDescriptor(
            kind,
            messageName,
            declaringType,
            ZLinkHandlerMethodInvokerFactory.Create(targetMethod),
            BuildArgumentPlan(targetMethod.GetParameters(), contextType),
            messageType,
            replyType,
            contextType,
            HasCancellationToken: true,
            groups,
            explicitChannelName);
    }

    private static ZLinkRouteHandlerEndpointDescriptor CreateRouteInterfaceDescriptor(
        Type declaringType,
        Type handlerInterface,
        ZLinkMessageKind kind,
        IReadOnlySet<string> groups,
        string? packetName)
    {
        var args = handlerInterface.GetGenericArguments();
        var messageType = args[0];
        var replyType = kind == ZLinkMessageKind.Request ? args[1] : null;

        var map = declaringType.GetInterfaceMap(handlerInterface);
        MethodInfo? targetMethod = null;
        for (var i = 0; i < map.InterfaceMethods.Length; i++)
        {
            if (map.InterfaceMethods[i].Name == nameof(IZLinkRouteSendHandler<object>.HandleAsync))
            {
                targetMethod = map.TargetMethods[i];
                break;
            }
        }

        if (targetMethod is null)
        {
            throw new ZLinkConfigurationException(
                $"Route handler '{declaringType.FullName}' does not implement HandleAsync for '{handlerInterface.Name}'.");
        }

        return new ZLinkRouteHandlerEndpointDescriptor(
            kind,
            packetName ?? ZLinkMessageNameResolver.ResolveFromType(messageType),
            declaringType,
            ZLinkHandlerMethodInvokerFactory.Create(targetMethod),
            messageType,
            replyType,
            groups);
    }

    private static ZLinkHandlerEndpointDescriptor CreateDescriptor(
        Type declaringType,
        MethodInfo method,
        string? messageNameOverride,
        ZLinkMessageKind kind,
        IReadOnlySet<string> groups)
    {
        var parameters = method.GetParameters();
        if (parameters.Length == 0)
        {
            throw new ZLinkConfigurationException(
                $"Handler method '{declaringType.FullName}.{method.Name}' must accept a message parameter.");
        }

        var messageType = parameters[0].ParameterType;
        var messageName = messageNameOverride ?? ZLinkMessageNameResolver.ResolveFromType(messageType);
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
            messageName,
            declaringType,
            ZLinkHandlerMethodInvokerFactory.Create(method),
            BuildArgumentPlan(parameters, contextType),
            messageType,
            replyType,
            contextType,
            hasCancellationToken,
            groups,
            null);
    }

    private static ZLinkHandlerArgumentKind[] BuildArgumentPlan(
        IReadOnlyList<ParameterInfo> parameters,
        Type? contextType)
    {
        var plan = new ZLinkHandlerArgumentKind[parameters.Count];
        if (parameters.Count == 0)
        {
            return plan;
        }

        plan[0] = ZLinkHandlerArgumentKind.Message;
        for (var i = 1; i < parameters.Count; i++)
        {
            if (parameters[i].ParameterType == typeof(CancellationToken))
            {
                plan[i] = ZLinkHandlerArgumentKind.CancellationToken;
                continue;
            }

            if (contextType is not null && parameters[i].ParameterType.IsAssignableFrom(contextType))
            {
                plan[i] = ZLinkHandlerArgumentKind.Context;
                continue;
            }

            plan[i] = ZLinkHandlerArgumentKind.Default;
        }

        return plan;
    }

    private static IReadOnlySet<string> ResolveGroups(Type declaringType)
    {
        return declaringType
            .GetCustomAttributes<ZLinkHandlerGroupAttribute>()
            .Select(static attribute => attribute.GroupName)
            .Where(static group => !string.IsNullOrWhiteSpace(group))
            .ToHashSet(StringComparer.Ordinal);
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

    private readonly record struct ZLinkEndpointAttributeDescriptor(
        ZLinkMessageKind Kind,
        string? PacketName);
}
