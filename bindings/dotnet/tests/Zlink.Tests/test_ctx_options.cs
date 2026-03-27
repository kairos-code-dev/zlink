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
        ctx.SetOption(ContextOption.IoThreads, 1);
        Assert.Equal(1, ctx.GetOption(ContextOption.IoThreads));

        int maxSockets = ctx.GetOption(ContextOption.MaxSockets);
        Assert.True(maxSockets > 0);
    }

    [Fact]
    public void context_default_limits_are_positive()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        Assert.True(ctx.GetOption(ContextOption.MaxSockets) > 0);
        Assert.True(ctx.GetOption(ContextOption.SocketLimit) > 0);
        Assert.True(ctx.GetOption(ContextOption.IoThreads) > 0);
        Assert.True(ctx.GetOption(ContextOption.MsgTSize) > 0);
    }

    [Fact]
    public void context_blocky_can_be_configured()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        Assert.Equal(1, ctx.GetOption(ContextOption.Blocky));
        ctx.SetOption(ContextOption.Blocky, 0);
        Assert.Equal(0, ctx.GetOption(ContextOption.Blocky));
    }

    [Fact]
    public void context_thread_options_accept_valid_values()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        ctx.SetOption(ContextOption.ThreadSchedPolicy, 0);
        Assert.Equal(0, ctx.GetOption(ContextOption.ThreadSchedPolicy));

        ctx.SetOption(ContextOption.ThreadAffinityCpuAdd, 0);
        ctx.SetOption(ContextOption.ThreadAffinityCpuRemove, 0);

        ctx.SetOption(ContextOption.ThreadNamePrefix, 1234);
        Assert.Equal(1234, ctx.GetOption(ContextOption.ThreadNamePrefix));
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

        ctx.SetOption(ContextOption.Blocky, 0);
        Assert.Equal(0, ctx.GetOption(ContextOption.Blocky));

        using var router = new RouterSocket(ctx);
        Assert.Equal(0, router.GetOption(SocketOptions.Linger));
    }

    [Fact]
    public void context_invalid_option_throws()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        Assert.Throws<ZlinkException>(() => ctx.SetOption((ContextOption)(-1), 0));
        Assert.Throws<ZlinkException>(() => ctx.GetOption((ContextOption)(-1)));
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
