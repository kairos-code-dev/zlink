using Systems.Zlink;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.UnitTests;

public sealed class TransportFailFastTests
{
    [Fact]
    public async Task NotConnected_Fails_Fast_On_AutoConnect_Managed_Submitters()
    {
        var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(5),
            CancellationToken.None,
            failFastNotConnected: () => true);

        var exception = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await submitter.Async(
                Message.From(new byte[] { 1 }),
                _ => throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.NotConnected)));

        Assert.Equal(ZLinkFrameworkErrorKind.RouteNotConnected, exception.Kind);
        Assert.True(exception.IsRetriable);
        await submitter.DisposeAsync();
    }

    [Fact]
    public async Task NotConnected_Still_Buffers_Without_The_Opt_In()
    {
        Action ready = () => { };
        var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(30),
            CancellationToken.None);

        var connected = false;
        var pending = submitter.Async(
            Message.From(new byte[] { 1 }),
            _ => connected
                ? true
                : throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.NotConnected));

        Assert.False(pending.IsCompleted);

        connected = true;
        ready();
        await pending;
        await submitter.DisposeAsync();
    }

    [Fact]
    public async Task Unawaited_Submit_Failures_Reach_The_Error_Sink()
    {
        Exception? seen = null;
        Action<Exception> handler = exception => seen = exception;
        ZLinkRuntimeErrorSink.UnhandledCallbackException += handler;
        try
        {
            ZLinkUnawaitedSubmit.Observe(
                ValueTask.FromException(new InvalidOperationException("submit died")),
                "test submit");

            for (var i = 0; i < 100 && seen is null; i++)
            {
                await Task.Delay(10);
            }

            Assert.IsType<InvalidOperationException>(seen);
        }
        finally
        {
            ZLinkRuntimeErrorSink.UnhandledCallbackException -= handler;
        }
    }

    [Fact]
    public void Unawaited_Submit_Cancellation_Is_Not_A_Failure()
    {
        Exception? seen = null;
        Action<Exception> handler = exception => seen = exception;
        ZLinkRuntimeErrorSink.UnhandledCallbackException += handler;
        try
        {
            ZLinkUnawaitedSubmit.Observe(
                ValueTask.FromException(new OperationCanceledException()),
                "test submit");

            Assert.Null(seen);
        }
        finally
        {
            ZLinkRuntimeErrorSink.UnhandledCallbackException -= handler;
        }
    }
}
