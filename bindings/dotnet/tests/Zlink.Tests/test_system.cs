using System;
using System.Threading;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_system
{
    [Fact]
    public void version_matches_core_header()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        var expected = CoreTestSupport.ReadCoreHeaderVersion();
        var actual = global::Systems.Zlink.Zlink.Version();

        Assert.Equal(expected.major, actual.Major);
        Assert.Equal(expected.minor, actual.Minor);
        Assert.Equal(expected.patch, actual.Patch);
    }

    [Fact]
    public void create_and_destroy_context_socket()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var socket = new PairSocket(ctx);

        var probe = new Received();
        Assert.False(socket.Recv(probe, RecvFlags.DontWait));
        Assert.True(Zlink.Has("tcp") || !Zlink.Has("tcp"));
    }

    [Fact]
    public void runtime_sleep_overloads_callable()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        Zlink.Sleep(TimeSpan.Zero);
    }

    [Fact]
    public void atomic_counter_basic_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var counter = new AtomicCounter();
        counter.Set(3);

        Assert.Equal(3, counter.Value);
        _ = counter.Increment();
        Assert.Equal(4, counter.Value);
        _ = counter.Decrement();
        Assert.Equal(3, counter.Value);
    }

    [Fact]
    public void stopwatch_basic_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var watch = new ZlinkStopwatch();

        ulong intermediate = watch.Intermediate();
        ulong elapsed = watch.Stop();

        Assert.True(elapsed >= intermediate);
        Assert.Throws<ObjectDisposedException>(() => watch.Intermediate());
    }

    [Fact]
    public void timer_basic_contract_uses_native_backend()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var timer = new Timer();
        timer.Start(TimeSpan.FromMilliseconds(5), 1);

        ulong? fireCount = timer.Recv();

        Assert.Equal(1UL, fireCount.GetValueOrDefault());
    }

    [Fact]
    public void timer_from_spot_uses_spot_scheduler_backend()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var node = new SpotNode(ctx);
        using var spot = node.CreateSpot();
        using var timer = Timer.FromSpot(spot);

        timer.Start(TimeSpan.FromMilliseconds(5), 1);

        ulong? fireCount = timer.Recv();

        Assert.Equal(1UL, fireCount.GetValueOrDefault());
    }

    [Fact]
    public void timer_on_fire_invokes_callback()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var timer = new Timer();
        using var fired = new ManualResetEventSlim(false);
        ulong observed = 0;

        timer.OnFire((_, fireCount) =>
        {
            observed = fireCount;
            fired.Set();
        });
        timer.Start(TimeSpan.FromMilliseconds(5), 1);

        Assert.True(fired.Wait(20000));
        Assert.Equal(1UL, observed);
    }
}
