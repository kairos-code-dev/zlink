using System.Reflection;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotDescriptorFactory
{
    private const string ActorJoinMethodName = "OnActorJoinAsync";

    public static ZLinkSpotDescriptor CreatePacketDescriptor(
        Type handlerType,
        Type expectedSpotType,
        string? packetName = null)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition == typeof(IZLinkSpotPacketHandler<,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    Invoker = CreateInvoker(handlerType),
                    MessageName = packetName ?? ZLinkMessageNameResolver.ResolveFromType(arguments[1])
                };
            }

            if (definition == typeof(IZLinkSpotRequestHandler<,,>))
            {
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    ReplyType = arguments[2],
                    Invoker = CreateInvoker(handlerType),
                    MessageName = packetName ?? ZLinkMessageNameResolver.ResolveFromType(arguments[1])
                };
            }
        }

        throw new InvalidOperationException(
            $"SPOT packet handler '{handlerType}' must implement IZLinkSpotPacketHandler<,> or IZLinkSpotRequestHandler<,,>.");
    }

    public static ZLinkSpotSubscriptionDescriptor CreateSubscriptionDescriptor(
        string topic,
        Type handlerType,
        Type expectedSpotType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkSpotSubscriptionHandler<,>)) continue;

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotSubscriptionDescriptor
            {
                Topic = topic,
                HandlerType = handlerType,
                SpotType = arguments[0],
                MessageType = arguments[1],
                Invoker = CreateInvoker(handlerType),
                MessageName = ZLinkMessageNameResolver.ResolveFromType(arguments[1])
            };
        }

        throw new InvalidOperationException(
            $"SPOT subscription handler '{handlerType}' must implement IZLinkSpotSubscriptionHandler<,>.");
    }

    public static ZLinkSpotTimerDescriptor CreateTimerDescriptor(
        string name,
        TimeSpan period,
        Type handlerType,
        Type expectedSpotType)
    {
        foreach (var (definition, arguments) in ZLinkHandlerContractInspector.EnumerateGenericInterfaces(handlerType))
        {
            if (definition != typeof(IZLinkSpotTimerHandler<>)) continue;

            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotTimerDescriptor
            {
                Name = name,
                Period = period,
                HandlerType = handlerType,
                SpotType = arguments[0],
                Invoker = CreateInvoker(handlerType)
            };
        }

        throw new InvalidOperationException(
            $"SPOT timer handler '{handlerType}' must implement IZLinkSpotTimerHandler<>.");
    }

    public static IEnumerable<ZLinkSpotActorJoinDescriptor> CreateSpotActorJoinDescriptors(Type spotType)
    {
        var contract = ZLinkSpotActorContractInspector.GetSpotOrEntryContract(spotType);
        foreach (var method in EnumerateActorJoinMethods(spotType, contract?.ContractType))
        {
            if (contract is null)
                throw new InvalidOperationException(
                    $"SPOT actor join hook '{spotType}' must implement IZLinkSpot<TActor> or IZLinkEntrySpot<TActor>.");

            yield return CreateSpotActorJoinDescriptor(spotType, method, contract.ActorType);
        }
    }

    private static void ValidateSpotType(Type handlerType, Type expectedSpotType, Type actualSpotType)
    {
        ZLinkHandlerContractDescriptorSupport.RequireExactType(
            handlerType,
            expectedSpotType,
            actualSpotType,
            "SPOT handler");
    }

    private static void ValidateActorType(Type handlerType, Type? expectedActorType, Type actualActorType)
    {
        if (expectedActorType is null)
        {
            if (!typeof(IZLinkActor).IsAssignableFrom(actualActorType))
                throw new InvalidOperationException(
                    $"SPOT actor join callback '{handlerType}' targets '{actualActorType}', but actor type must implement '{typeof(IZLinkActor)}'.");

            return;
        }

        ZLinkHandlerContractDescriptorSupport.RequireExactType(
            handlerType,
            expectedActorType,
            actualActorType,
            "SPOT actor join callback");
    }

    private static ZLinkHandlerMethodInvoker CreateInvoker(Type handlerType)
    {
        return ZLinkHandlerContractDescriptorSupport.CreateHandleAsyncInvoker(handlerType, "Handler");
    }

    private static ZLinkSpotActorJoinDescriptor CreateSpotActorJoinDescriptor(
        Type spotType,
        MethodInfo method,
        Type expectedActorType)
    {
        var parameters = ZLinkHandlerMethodShape.RequireParameterCount(
            spotType,
            method,
            3,
            "SPOT actor join hook");
        if (parameters[0].ParameterType != typeof(string))
            throw new InvalidOperationException(
                $"SPOT actor join hook '{spotType}' method '{method.Name}' must use string actorId as the first parameter.");

        if (parameters[1].ParameterType != typeof(ZLinkMessage))
            throw new InvalidOperationException(
                $"SPOT actor join hook '{spotType}' method '{method.Name}' must use ZLinkMessage as the second parameter.");

        ZLinkHandlerMethodShape.RequireCancellationToken(
            spotType,
            method,
            parameters[2],
            "SPOT actor join hook",
            "third");
        if (method.ReturnType != typeof(ValueTask<ZLinkSpotActorJoinResult>))
            throw new InvalidOperationException(
                $"SPOT actor join hook '{spotType}' method '{method.Name}' must return ValueTask<ZLinkSpotActorJoinResult>.");

        return new ZLinkSpotActorJoinDescriptor
        {
            HandlerType = spotType,
            SpotType = spotType,
            ActorType = expectedActorType,
            Invoker = ZLinkHandlerMethodInvokerFactory.Create(method),
            PassSpotArgument = false
        };
    }

    private static IEnumerable<MethodInfo> EnumerateActorJoinMethods(Type spotType, Type? contractType)
    {
        var declaredMethods = spotType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Where(method => method.Name == ActorJoinMethodName
                             && method.DeclaringType == spotType)
            .ToArray();
        if (declaredMethods.Length > 0) return declaredMethods;

        return contractType is null
            ? []
            : ZLinkSpotActorContractInspector.EnumerateInterfaceMethods(contractType)
                .Where(method => method.Name == ActorJoinMethodName);
    }
}
