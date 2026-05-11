using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Handlers.Internal;

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

            foreach (var iface in type.GetInterfaces())
            {
                if (!iface.IsGenericType)
                {
                    continue;
                }

                var def = iface.GetGenericTypeDefinition();
                if (def == typeof(IZLinkRequestHandler<,>))
                {
                    endpoints.Add(CreateInterfaceDescriptor(type, iface, ZLinkMessageKind.Request));
                }
                else if (def == typeof(IZLinkSendHandler<>))
                {
                    endpoints.Add(CreateInterfaceDescriptor(type, iface, ZLinkMessageKind.Command));
                }
                else if (def == typeof(IZLinkEventHandler<>))
                {
                    endpoints.Add(CreateInterfaceDescriptor(type, iface, ZLinkMessageKind.Event));
                }
            }
        }

        return endpoints;
    }

    private static ZLinkHandlerEndpointDescriptor CreateInterfaceDescriptor(
        Type declaringType,
        Type handlerInterface,
        ZLinkMessageKind kind)
    {
        var args = handlerInterface.GetGenericArguments();
        var messageType = args[0];
        var replyType = kind == ZLinkMessageKind.Request ? args[1] : null;

        var map = declaringType.GetInterfaceMap(handlerInterface);
        MethodInfo? targetMethod = null;
        for (var i = 0; i < map.InterfaceMethods.Length; i++)
        {
            if (map.InterfaceMethods[i].Name == nameof(IZLinkEventHandler<object>.HandleAsync))
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

        var messageName = ZLinkMessageNameResolver.ResolveFromType(messageType);
        var contextType = kind switch
        {
            ZLinkMessageKind.Request => typeof(ZLinkRequestContext),
            ZLinkMessageKind.Command => typeof(ZLinkSendContext),
            _ => typeof(ZLinkEventContext),
        };

        return new ZLinkHandlerEndpointDescriptor(
            kind,
            messageName,
            declaringType,
            targetMethod,
            messageType,
            replyType,
            contextType,
            HasCancellationToken: true);
    }

    private static ZLinkHandlerEndpointDescriptor CreateDescriptor(
        Type declaringType,
        MethodInfo method,
        string? messageNameOverride,
        ZLinkMessageKind kind)
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
