using System.Reflection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Workers;
using Zlink.Framework.ContractTests.Support;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Locations.Redis;
using Zlink.HttpClient;

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
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Configuration.IZLinkDrainControl"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Configuration.ZLinkDrainResult"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Configuration.ZLinkMeshDrainResult"));
        Assert.Null(assembly.GetType("Zlink.Framework.Contracts.Configuration.ZLinkMeshDrainSnapshot"));

        Assert.DoesNotContain(typeof(IZLinkSendCall).GetMethods(), method => method.Name == "PacketName");
        Assert.DoesNotContain(typeof(IZLinkRequestCall).GetMethods(), method => method.Name == "PacketName");
        Assert.Contains(typeof(IZLinkRequestCall).GetMethods(), method => method.Name == "Yield");
        Assert.DoesNotContain(typeof(IZLinkActorSendCall).GetMethods(), method => method.Name == "PacketName");
        Assert.Contains(typeof(IZLinkActorSendCall).GetMethods(), method => method.Name == "Async");
        Assert.DoesNotContain(typeof(IZLinkActorRequestCall).GetMethods(), method => method.Name == "PacketName");
        Assert.Equal(
            new[] { "Defer" },
            typeof(IZLinkActorDeferredJoinCall).GetMethods().Select(method => method.Name).ToArray());
        Assert.DoesNotContain(typeof(IZLinkActorContext).GetMembers(), member => member.Name is "IsJoined" or "GetSpot");
        Assert.Contains(typeof(IZLinkWorkerCall<>).GetMethods(), method => method.Name == "Yield");
        Assert.Contains(typeof(IZLinkWorkerCall<>).GetMethods(), method => method.Name == "Submit");
        Assert.DoesNotContain(typeof(IZLinkDispatchOptions).GetProperties(), property => property.Name.EndsWith("DispatchMode", StringComparison.Ordinal));

        Assert.True(typeof(ZLinkActorJoinCompletion).IsAbstract);
        Assert.True(typeof(ZLinkActorJoinCompletion.Accepted).IsSealed);
        Assert.True(typeof(ZLinkActorJoinCompletion.Rejected).IsSealed);
        Assert.True(typeof(ZLinkActorJoinCompletion.Failed).IsSealed);
        Assert.Empty(typeof(SpotHandle).GetConstructors());
        Assert.Equal(
            new[] { "Connect", "Disconnect", "ListConnections" },
            typeof(IZLinkEndpointConnections).GetMethods().Select(method => method.Name).Order().ToArray());
    }

    [Fact]
    public void Closed_result_and_event_roots_cannot_be_subclassed_outside_the_framework_assembly()
    {
        Type[] roots =
        [
            typeof(ZLinkActorJoinCompletion),
            typeof(ZLinkLocationRuntimeEvent),
            typeof(ZLinkLocationPeerEvent), typeof(ZLinkLocationSpotEvent),
            typeof(ZLinkLocationActorEvent),
            typeof(ZLinkSpotEvent), typeof(ZLinkRoutingIdSlotAcquireResult)
        ];

        foreach (var root in roots)
        {
            var constructors = root.GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic)
                .Where(constructor => constructor.GetParameters() is not [{ ParameterType: var parameterType }]
                                      || parameterType != root)
                .ToArray();
            Assert.NotEmpty(constructors);
            Assert.All(constructors, constructor => Assert.True(constructor.IsFamilyAndAssembly));
        }
    }

    [Fact]
    public void Redis_Extension_Remains_A_Separate_Package_Without_A_Backend_Specific_Registration_API()
    {
        var framework = typeof(IZLinkFrameworkOptions).Assembly;
        var redis = typeof(ZLinkRedisLocationStore).Assembly;

        Assert.NotSame(framework, redis);
        Assert.DoesNotContain(
            framework.GetReferencedAssemblies(),
            reference => reference.Name == "StackExchange.Redis");
        Assert.DoesNotContain(
            framework.GetExportedTypes(),
            type => type.Namespace?.Contains("Redis", StringComparison.Ordinal) == true);
        Assert.DoesNotContain(
            framework.GetExportedTypes().SelectMany(static type => type.GetMethods()),
            method => method.Name.Contains("Redis", StringComparison.Ordinal));
    }

    [Fact]
    public void Every_public_contract_interface_has_a_scenario_example()
    {
        var exportedContractTypes = new[]
            {
                typeof(IZLinkFrameworkOptions).Assembly,
                typeof(Zlink.Framework.Contracts.Codecs.IZLinkCodecExtension).Assembly
            }
            .Distinct()
            .SelectMany(static assembly => assembly.GetExportedTypes())
            .Where(static type => type.Namespace is not null
                                  && type.Namespace.StartsWith("Zlink.Framework.Contracts", StringComparison.Ordinal))
            .ToHashSet();

        // Every exported interface must have a worked example. Closed-union abstract records
        // (ZLinkActorCreateResult, ZLinkActorJoinCompletion, ...) are equally part of the public
        // contract surface and may be cited by an example, but citing them is not mandatory --
        // so they widen the "is this a real contract type" check without widening the mandate.
        var exportedContracts = exportedContractTypes
            .Where(static type => type.IsInterface)
            .OrderBy(static type => type.FullName, StringComparer.Ordinal)
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
            .Where(type => !exportedContractTypes.Contains(type))
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
    public void Fixed_spec_snapshot_matches_every_exported_contract_signature()
    {
        var repositoryRoot = FindRepositoryRoot();
        // 기계 판독용 계약 snapshot은 문서 트리가 아니라 .NET 코드 옆(framework/languages/dotnet/contract)에 둔다.
        var contractRoot = Path.Combine(
            repositoryRoot,
            "framework",
            "languages",
            "dotnet",
            "contract");
        var assemblies = new[]
            {
                typeof(IZLinkFrameworkOptions).Assembly,
                typeof(Zlink.Framework.Contracts.Codecs.IZLinkCodecExtension).Assembly,
                typeof(ServiceCollectionExtensions).Assembly,
                typeof(ZLinkMessagePackCodec).Assembly,
                typeof(ZLinkProtobufCodec).Assembly,
                typeof(ZLinkRedisLocationStore).Assembly,
                typeof(ZLinkHttpClient).Assembly,
                typeof(IZlinkStreamConnector).Assembly
            }
            .Distinct()
            .ToArray();
        var snapshotRoot = Path.Combine(contractRoot, "api");
        var expected = string.Concat(assemblies
            .Select(static assembly => assembly.GetName().Name!)
            .Order(StringComparer.Ordinal)
            .Select(name => File.ReadAllText(Path.Combine(snapshotRoot, $"{name}.api.txt"))));
        var actual = PublicContractSnapshot.Render(assemblies);

        Assert.Equal(NormalizeLines(expected), NormalizeLines(actual));
    }

    private static string NormalizeLines(string value) =>
        value.Replace("\r\n", "\n", StringComparison.Ordinal);

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
