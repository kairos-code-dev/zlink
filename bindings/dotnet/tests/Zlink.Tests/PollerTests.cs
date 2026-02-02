using System.Collections.Generic;
using Xunit;

namespace Zlink.Tests;

public class PollerTests
{
    [Fact]
    public void PollerWaitDoesNotThrow()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        try
        {
            using var ctx = new Context();
            using var sock = new Socket(ctx, SocketType.Pair);
            var poller = new Poller();
            poller.Add(sock, PollEvents.PollIn);
            var events = new List<PollEvent>();
            int rc = poller.Wait(events, 0);
            Assert.True(rc >= 0);
        }
        catch (EntryPointNotFoundException)
        {
        }
    }
}
