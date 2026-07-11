using System.Reflection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Workers;
using Zlink.Framework.ContractTests.Support;
using Systems.Zlink.Stream.Connector.Contracts;

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

    [Fact]
    public void Canonical_interface_documents_cover_every_exported_contract_type()
    {
        var repositoryRoot = FindRepositoryRoot();
        var specRoot = Path.Combine(
            repositoryRoot,
            "framework",
            "doc",
            "framework",
            "common",
            "spec",
            "languages",
            "dotnet");
        var documents = Directory.GetFiles(specRoot, "*.ko.md", SearchOption.TopDirectoryOnly)
            .Where(path => !path.EndsWith("stage-wrapper-on-spot.ko.md", StringComparison.Ordinal))
            .Select(File.ReadAllText)
            .ToArray();
        Assert.NotEmpty(documents);
        var canonicalText = string.Join(Environment.NewLine, documents);

        var exportedContracts = typeof(IZLinkFrameworkOptions).Assembly.GetExportedTypes()
            .Where(static type => type.Namespace?.StartsWith(
                "Zlink.Framework.Contracts",
                StringComparison.Ordinal) == true)
            .Concat(typeof(IZlinkStreamConnector).Assembly.GetExportedTypes()
                .Where(static type => type.Namespace?.StartsWith(
                    "Systems.Zlink.Stream.Connector.Contracts",
                    StringComparison.Ordinal) == true))
            .OrderBy(static type => type.FullName, StringComparer.Ordinal)
            .ToArray();

        var missing = exportedContracts
            .Where(type => !canonicalText.Contains(
                type.Name.Split('`')[0],
                StringComparison.Ordinal))
            .Select(static type => type.FullName)
            .ToArray();

        Assert.True(
            missing.Length == 0,
            $"Canonical .NET interface documents are missing exported contract types:{Environment.NewLine}{string.Join(Environment.NewLine, missing)}");
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "AGENTS.md")))
                return current.FullName;
            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the repository root containing AGENTS.md.");
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
