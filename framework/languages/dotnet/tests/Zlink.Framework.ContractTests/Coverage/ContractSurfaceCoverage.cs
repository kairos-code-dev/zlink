using System.Reflection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Workers;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Coverage;

public sealed class ContractSurfaceCoverage
{
    [Fact]
    public void Frozen_public_surface_excludes_replaced_contracts()
    {
        var assembly = typeof(IZLinkFrameworkOptions).Assembly;
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Channels.IZLinkYieldRequestCall"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Actors.IZLinkActorYieldJoinCall"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Dispatch.ZLinkDispatchMode"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Locations.SpotRef"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Locations.IZLinkSpotRefResolver"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Locations.IZLinkActorAddressResolver"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Assembly.ZLinkFrameworkAssemblyMarker"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Codecs.Json.ZLinkJsonCodecNamespace"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Handlers.ZLinkStreamRawAttribute"));

        Assert.DoesNotContain(typeof(IZLinkSendCall).GetMethods(), method => method.Name == "PacketName");
        Assert.DoesNotContain(typeof(IZLinkRequestCall).GetMethods(), method => method.Name is "PacketName" or "Yield");
        Assert.DoesNotContain(typeof(IZLinkActorSendCall).GetMethods(), method => method.Name is "PacketName" or "Async");
        Assert.DoesNotContain(typeof(IZLinkActorRequestCall).GetMethods(), method => method.Name == "PacketName");
        Assert.DoesNotContain(typeof(IZLinkActorJoinCall).GetMethods(), method => method.Name == "Yield");
        Assert.DoesNotContain(typeof(IZLinkActorContext).GetMembers(), member => member.Name is "IsJoined" or "GetSpot");
        Assert.DoesNotContain(typeof(IZLinkWorkerCall<>).GetMethods(), method => method.Name is "Yield" or "Submit");
        Assert.DoesNotContain(typeof(IZLinkDispatchOptions).GetProperties(), property => property.Name.EndsWith("DispatchMode", StringComparison.Ordinal));

        Assert.True(typeof(ZLinkActorJoinResult).IsAbstract);
        Assert.True(typeof(ZLinkActorJoinResult.Accepted).IsSealed);
        Assert.True(typeof(ZLinkActorJoinResult.Rejected).IsSealed);
        Assert.Empty(typeof(SpotHandle).GetConstructors());
        Assert.Equal(
            new[] { "Connect", "Disconnect", "ListConnections" },
            typeof(IZLinkEndpointConnections).GetMethods().Select(method => method.Name).Order().ToArray());
    }

    [Fact]
    public void Every_public_contract_interface_has_a_scenario_example()
    {
        var exportedContracts = typeof(IZLinkFrameworkOptions).Assembly
            .GetExportedTypes()
            .Where(type => type is { IsInterface: true, Namespace: not null }
                           && type.Namespace.StartsWith("Zlink.Framework.Contracts", StringComparison.Ordinal))
            .OrderBy(type => type.FullName, StringComparer.Ordinal)
            .ToArray();

        var coveredContracts = typeof(ContractExampleAttribute).Assembly
            .GetTypes()
            .SelectMany(type => type.GetMethods(
                BindingFlags.Instance |
                BindingFlags.Public |
                BindingFlags.NonPublic |
                BindingFlags.Static))
            .Where(method => method.GetCustomAttribute<FactAttribute>() is not null)
            .SelectMany(method => method.GetCustomAttributes<ContractExampleAttribute>())
            .SelectMany(attribute => attribute.Contracts)
            .ToHashSet();

        var unknown = coveredContracts
            .Where(type => !exportedContracts.Contains(type))
            .OrderBy(type => type.FullName, StringComparer.Ordinal)
            .Select(type => type.FullName)
            .ToArray();

        var missing = exportedContracts
            .Where(type => !coveredContracts.Contains(type))
            .Select(type => type.FullName)
            .ToArray();

        Assert.Empty(unknown);
        Assert.Empty(missing);
    }

    [Fact]
    public void Basic_business_message_contracts_do_not_expose_binding_messages()
    {
        var bindingMessage = typeof(Message);
        var frameworkMessage = typeof(ZLinkMessage);

        AssertMethodParameter(
            typeof(IZLinkSession),
            nameof(IZLinkSession.OnDispatchAsync),
            "payload",
            frameworkMessage,
            bindingMessage);
        AssertMethodParameterIsNot(
            typeof(IZLinkSessionPacketHandler<,>),
            nameof(IZLinkSessionPacketHandler<IZLinkSessionContext, ZLinkMessage>.HandleAsync),
            "message",
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkActorContext),
            nameof(IZLinkActorContext.JoinSpot),
            "request",
            frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkActorContext),
            nameof(IZLinkActorContext.JoinEntrySpot),
            "request",
            frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkSpot),
            nameof(IZLinkSpot.OnCreateAsync),
            "request",
            frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkSpot<>),
            nameof(IZLinkSpot<IZLinkActor>.OnActorJoinAsync),
            "request",
            frameworkMessage,
            bindingMessage);

        Assert.DoesNotContain(
            typeof(ZLinkSpotCreateResponse).GetMethods(BindingFlags.Public | BindingFlags.Static),
            method => method.GetParameters().Any(parameter => parameter.ParameterType == bindingMessage));
    }

    private static void AssertMethodParameter(
        Type contractType,
        string methodName,
        string parameterName,
        Type requiredParameterType,
        Type disallowedParameterType)
    {
        var matchingMethods = EnumerateInterfaceMethods(contractType)
            .Where(method => method.Name == methodName
                             && method.GetParameters().Any(parameter => parameter.Name == parameterName))
            .ToArray();

        Assert.NotEmpty(matchingMethods);

        Assert.Contains(
            matchingMethods,
            method => method.GetParameters()
                .Single(parameter => parameter.Name == parameterName)
                .ParameterType == requiredParameterType);

        foreach (var method in matchingMethods)
        {
            var parameter = Assert.Single(
                method.GetParameters(),
                parameter => parameter.Name == parameterName);
            Assert.NotEqual(disallowedParameterType, parameter.ParameterType);
        }
    }

    private static void AssertMethodParameterIsNot(
        Type contractType,
        string methodName,
        string parameterName,
        Type disallowedParameterType)
    {
        var matchingMethods = EnumerateInterfaceMethods(contractType)
            .Where(method => method.Name == methodName
                             && method.GetParameters().Any(parameter => parameter.Name == parameterName))
            .ToArray();

        Assert.NotEmpty(matchingMethods);

        foreach (var method in matchingMethods)
        {
            var parameter = Assert.Single(
                method.GetParameters(),
                parameter => parameter.Name == parameterName);
            Assert.NotEqual(disallowedParameterType, parameter.ParameterType);
        }
    }

    private static IEnumerable<MethodInfo> EnumerateInterfaceMethods(Type interfaceType)
    {
        foreach (var method in interfaceType.GetMethods())
            yield return method;

        foreach (var inheritedInterface in interfaceType.GetInterfaces())
        {
            foreach (var method in inheritedInterface.GetMethods())
                yield return method;
        }
    }
}
