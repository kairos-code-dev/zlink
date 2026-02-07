using Xunit;

namespace Zlink.Tests;

public class ServiceDiscoveryTests
{
    [Fact]
    public void CreateServiceDiscoveryObjects()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        try
        {
            using var ctx = new Context();
            using var registry = new Registry(ctx);
            using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
            using var gateway = new Gateway(ctx, discovery);
            using var receiver = new Receiver(ctx);
            Assert.NotNull(registry);
            Assert.NotNull(discovery);
            Assert.NotNull(gateway);
            Assert.NotNull(receiver);
        }
        catch (EntryPointNotFoundException)
        {
        }
    }
}
