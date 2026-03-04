using Xunit;

namespace Zlink.Tests;

public sealed class test_ctx_options
{
    private const ContextOption CtxThreadSchedPolicy = (ContextOption)4;
    private const ContextOption CtxThreadAffinityCpuAdd = (ContextOption)7;
    private const ContextOption CtxThreadAffinityCpuRemove = (ContextOption)8;
    private const ContextOption CtxThreadNamePrefix = (ContextOption)9;
    private const ContextOption CtxIpv6 = (ContextOption)42;
    private const ContextOption CtxBlocky = (ContextOption)70;

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
        Assert.Equal(0, ctx.GetOption(CtxIpv6));
    }

    [Fact]
    public void context_ipv6_option_propagates_to_new_sockets()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        ctx.SetOption(CtxIpv6, 1);
        Assert.Equal(1, ctx.GetOption(CtxIpv6));

        using var router = new Socket(ctx, SocketType.Router);
        Assert.Equal(1, router.GetOption(SocketOptions.Ipv6));
    }

    [Fact]
    public void context_thread_options_accept_valid_values()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        ctx.SetOption(CtxThreadSchedPolicy, 0);
        Assert.Equal(0, ctx.GetOption(CtxThreadSchedPolicy));

        ctx.SetOption(CtxThreadAffinityCpuAdd, 0);
        ctx.SetOption(CtxThreadAffinityCpuRemove, 0);

        ctx.SetOption(CtxThreadNamePrefix, 1234);
        Assert.Equal(1234, ctx.GetOption(CtxThreadNamePrefix));
    }

    [Fact]
    public void context_blocky_changes_default_socket_linger()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using (var preRouter = new Socket(ctx, SocketType.Router))
        {
            Assert.Equal(-1, preRouter.GetOption(SocketOptions.Linger));
        }

        ctx.SetOption(CtxBlocky, 0);
        Assert.Equal(0, ctx.GetOption(CtxBlocky));

        using var router = new Socket(ctx, SocketType.Router);
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
