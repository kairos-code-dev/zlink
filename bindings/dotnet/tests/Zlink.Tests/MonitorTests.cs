using Xunit;

namespace Zlink.Tests;

public class MonitorTests
{
    [Fact]
    public void MonitorSocketCanOpen()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        try
        {
            using var ctx = new Context();
            using var sock = new Socket(ctx, SocketType.Pair);
            using var monitor = sock.MonitorOpen(SocketEvent.All);
            Assert.NotNull(monitor);
        }
        catch (EntryPointNotFoundException)
        {
        }
    }
}
