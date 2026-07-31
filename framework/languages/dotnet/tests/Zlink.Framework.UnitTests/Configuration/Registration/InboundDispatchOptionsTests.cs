using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class InboundDispatchOptionsTests
{
    [Theory]
    [InlineData(ZLinkApplicationHwmProfile.Compact, 20UL)]
    [InlineData(ZLinkApplicationHwmProfile.LowLatency, 50UL)]
    [InlineData(ZLinkApplicationHwmProfile.Balanced, 100UL)]
    [InlineData(ZLinkApplicationHwmProfile.Throughput, 200UL)]
    public void Auto_Hwm_Uses_The_Exact_Profile_Ratio(
        ZLinkApplicationHwmProfile profile,
        ulong expected)
    {
        var options = new ZLinkInboundDispatchOptionsModel
        {
            ApplicationHwmProfile = profile,
            ProcessMemoryLimitBytes = 1_000
        };

        Assert.Equal(expected, ZLinkApplicationHwmResolver.Resolve(options));
    }

    [Fact]
    public void Explicit_Hwm_Does_Not_Require_A_Process_Memory_Limit()
    {
        var options = new ZLinkInboundDispatchOptionsModel
        {
            ApplicationHwmBytes = 0
        };

        Assert.Equal(0UL, ZLinkApplicationHwmResolver.Resolve(options));
    }

    [Fact]
    public void Invalid_Profile_And_Zero_Memory_Limit_Are_Rejected()
    {
        var options = new ZLinkInboundDispatchOptionsModel();

        Assert.Throws<ZLinkConfigurationException>(() =>
            options.ApplicationHwmProfile = (ZLinkApplicationHwmProfile)99);
        Assert.Throws<ZLinkConfigurationException>(() =>
            options.ProcessMemoryLimitBytes = 0);
    }

    [Fact]
    public void Memory_Limited_Mode_Requires_A_Finite_Listener_Maximum()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.ConfigureInboundDispatch().ProcessMemoryLimitBytes = 1_000_000;
            options.AddRouteMesh("mesh").Listen();
        });
        using var provider = services.BuildServiceProvider();

        var error = Assert.Throws<ZLinkConfigurationException>(() =>
            ZLinkFrameworkRegistrationValidator.ValidateInboundDispatch(
                provider.GetRequiredService<ZLinkFrameworkRegistration>()));

        Assert.Contains("MaxMessageSize", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Explicit_Unlimited_Mode_Does_Not_Require_A_Listener_Maximum()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.ConfigureInboundDispatch().ApplicationHwmBytes = 0;
            options.AddRouteMesh("mesh").Listen();
        });

        using var provider = services.BuildServiceProvider();
        ZLinkFrameworkRegistrationValidator.ValidateInboundDispatch(
            provider.GetRequiredService<ZLinkFrameworkRegistration>());
        Assert.Equal(0UL, provider.GetRequiredService<ZLinkFrameworkRegistration>()
            .InboundDispatchOptions.EffectiveApplicationHwmBytes);
    }

    [Fact]
    public void Application_Listener_Default_Is_A_Finite_Sixteen_Mebibytes()
    {
        Assert.Equal(16L * 1024L * 1024L, new ZLinkSocketConfig().MaxMessageSize);

        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.ConfigureInboundDispatch().ProcessMemoryLimitBytes = 100_000_000;
            options.AddRouteMesh("mesh").Listen();
        });
        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();

        ZLinkFrameworkRegistrationValidator.ValidateInboundDispatch(registration);

        Assert.Equal(10_000_000UL,
            registration.InboundDispatchOptions.EffectiveApplicationHwmBytes);
        Assert.Equal(16L * 1024L * 1024L,
            registration.SpotNodes["mesh"].Router!.SocketConfig.MaxMessageSize);
    }
}
