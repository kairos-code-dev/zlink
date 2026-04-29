using Xunit;

namespace Zlink.Tests;

public sealed class test_ctx_options
{
    [Fact]
    public void can_set_and_get_context_options()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        ctx.Options.IoThreads = 1;
        Assert.Equal(1, ctx.Options.IoThreads);

        int maxSockets = ctx.Options.MaxSockets;
        Assert.True(maxSockets > 0);

        ctx.Options.AutoHwmProfile = AutoHwmProfile.Throughput;
        Assert.Equal(AutoHwmProfile.Throughput, ctx.Options.AutoHwmProfile);
    }

    [Fact]
    public void context_default_limits_are_positive()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        Assert.True(ctx.Options.MaxSockets > 0);
        Assert.True(ctx.Options.SocketLimit > 0);
        Assert.True(ctx.Options.IoThreads > 0);
        Assert.True(ctx.Options.MessageThreadSize > 0);
    }

    [Fact]
    public void context_blocky_can_be_configured()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        Assert.True(ctx.Options.Blocky);
        ctx.Options.Blocky = false;
        Assert.False(ctx.Options.Blocky);
    }

    [Fact]
    public void context_thread_options_accept_valid_values()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        ctx.Options.ThreadSchedulingPolicy = 0;
        Assert.Equal(0, ctx.Options.ThreadSchedulingPolicy);

        ctx.Options.AddThreadAffinityCpu(0);
        ctx.Options.RemoveThreadAffinityCpu(0);
    }

    [Fact]
    public void context_blocky_changes_default_socket_linger()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using (var preRouter = new RouterSocket(ctx))
        {
            Assert.Equal(-1, preRouter.GetOption(SocketOptions.Linger));
        }

        ctx.Options.Blocky = false;
        Assert.False(ctx.Options.Blocky);

        using var router = new RouterSocket(ctx);
        Assert.Equal(0, router.GetOption(SocketOptions.Linger));
    }

    [Fact]
    public void context_invalid_option_throws()
    {
        using var ctx = new Context();
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ctx.SetOption((ContextOption)(-1), 0));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ctx.GetOption((ContextOption)(-1)));
    }

    [Fact]
    public void shutdown_context_is_callable()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        ctx.Shutdown();
    }
}
