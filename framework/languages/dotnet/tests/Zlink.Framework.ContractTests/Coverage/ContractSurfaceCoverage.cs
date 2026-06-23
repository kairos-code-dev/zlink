using System.Reflection;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Coverage;

public sealed class ContractSurfaceCoverage
{
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
        var frameworkMessage = typeof(Zlink.Framework.Contracts.Messaging.ZLinkMessage);

        AssertMethodParameter(
            typeof(IZLinkSession),
            nameof(IZLinkSession.OnDispatchAsync),
            "payload",
            requiredParameterType: frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkSessionPacketHandler<>),
            nameof(IZLinkSessionPacketHandler<IZLinkSessionContext>.HandleAsync),
            "payload",
            requiredParameterType: frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkActorContext),
            nameof(IZLinkActorContext.JoinSpot),
            "request",
            requiredParameterType: frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkActorContext),
            nameof(IZLinkActorContext.JoinEntrySpot),
            "request",
            requiredParameterType: frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkSpot),
            nameof(IZLinkSpot.OnCreateAsync),
            "request",
            requiredParameterType: frameworkMessage,
            bindingMessage);
        AssertMethodParameter(
            typeof(IZLinkSpot<>),
            nameof(IZLinkSpot<IZLinkActor>.OnActorJoinAsync),
            "request",
            requiredParameterType: frameworkMessage,
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
        var matchingMethods = contractType.GetMethods()
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
}
