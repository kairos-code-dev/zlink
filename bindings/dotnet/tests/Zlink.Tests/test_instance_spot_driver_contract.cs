// SPDX-License-Identifier: MPL-2.0

using System.Reflection;
using Zlink.Runtime.Service.InstanceSpots;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_instance_spot_driver_contract
{
    [Fact]
    public void ordinary_spot_surface_keeps_driver_details_out()
    {
        Assert.Equal(3, (int)SpotKind.Instance);
        Assert.Equal(3, (int)SpotActivationState.Closing);
        Assert.Equal(14, (int)MeshRecordKind.InstanceSpotActivation);
        Assert.Equal(113, (int)RequestResult.Backpressured);

        Assert.DoesNotContain(typeof(ISpot).GetMethods(), method =>
            method.Name.Contains("Placement", StringComparison.Ordinal)
            || method.Name.Contains("Activation", StringComparison.Ordinal));
        Assert.DoesNotContain(typeof(ISpot).Assembly.GetExportedTypes(), type =>
            type.Name.Contains("ActivationToken", StringComparison.Ordinal)
            || type.Name.Contains("ClaimRole", StringComparison.Ordinal));
    }

    [Fact]
    public void driver_namespace_exposes_reduced_fixed_contract()
    {
        Assert.Equal("Zlink.Runtime.Service.InstanceSpots",
            typeof(InstanceSpotPlacement).Namespace);
        Assert.Equal(
            new[]
            {
                "NodeRid", "NodeGeneration", "SpotRid", "InstanceSpotType",
                "MessageContractId"
            },
            PublicProperties(typeof(InstanceSpotPlacement)));
        Assert.Equal(
            new[]
            {
                "SpotRid", "OperationKind", "InstanceSpotType",
                "MessageContractId"
            },
            PublicProperties(typeof(InstanceSpotActivationData)));
        Assert.Equal(
            new[] { "Abort", "ClaimOwner", "Data", "MarkReady", "Redirect" },
            typeof(IInstanceSpotActivation)
                .GetMembers(BindingFlags.Instance | BindingFlags.Public)
                .Where(member => member.MemberType is MemberTypes.Property
                    or MemberTypes.Method)
                .Where(member => !member.Name.StartsWith("get_",
                    StringComparison.Ordinal))
                .Select(member => member.Name)
                .Order()
                .ToArray());
        Assert.Equal(
            new[] { "BeginClose", "Renew" },
            typeof(IInstanceSpotOwnerAdmission)
                .GetMethods()
                .Select(method => method.Name)
                .Order()
                .ToArray());
    }

    [Fact]
    public void driver_uses_async_framework_terminators_without_try_submit()
    {
        Assert.DoesNotContain(typeof(InstanceSpotDriver).GetMethods(), method =>
            method.Name.Contains("TrySubmit", StringComparison.Ordinal));
        Assert.DoesNotContain(Enum.GetNames<ZLinkRequestResult>(), name =>
            string.Equals(name, "Ok", StringComparison.Ordinal));
    }

    private static string[] PublicProperties(Type type)
    {
        return type.GetProperties(BindingFlags.Instance | BindingFlags.Public)
            .Select(property => property.Name)
            .ToArray();
    }
}
