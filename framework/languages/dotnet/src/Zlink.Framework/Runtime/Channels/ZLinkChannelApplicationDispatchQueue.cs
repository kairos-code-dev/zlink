using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelApplicationDispatchQueue : IAsyncDisposable
{
    private const int Capacity = 1024;
    private static readonly TimeSpan ShutdownJoinTimeout =
        TimeSpan.FromSeconds(1);
    private readonly Channel<DispatchWork> _queue = Channel.CreateBounded<DispatchWork>(
        new BoundedChannelOptions(Capacity)
        {
            FullMode = BoundedChannelFullMode.Wait,
            SingleReader = true,
            SingleWriter = true,
            AllowSynchronousContinuations = false
        });
    private readonly IZLinkRuntimeFailureReporter _errorSink;
    private readonly CancellationTokenSource _stop;
    private readonly string _name;
    private readonly Task _worker;
    private int _stopped;

    internal ZLinkChannelReplyGate ReplyGate { get; } = new();

    internal ZLinkChannelApplicationDispatchQueue(
        string name,
        IZLinkRuntimeFailureReporter errorSink,
        CancellationToken laneCancellationToken)
    {
        _name = name;
        _errorSink = errorSink;
        _stop = CancellationTokenSource.CreateLinkedTokenSource(
            laneCancellationToken);
        _worker = Task.Run(
            () => RunAsync(_stop.Token).AsTask(),
            CancellationToken.None);
    }

    internal bool TryPost(
        Func<CancellationToken, ValueTask> dispatch,
        Action reject)
    {
        ArgumentNullException.ThrowIfNull(dispatch);
        ArgumentNullException.ThrowIfNull(reject);
        var work = new DispatchWork(dispatch, reject);
        if (Volatile.Read(ref _stopped) == 0 && _queue.Writer.TryWrite(work))
            return true;

        Reject(work);
        return false;
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _stopped, 1) != 0)
            return;

        ReplyGate.Close();
        _queue.Writer.TryComplete();
        while (_queue.Reader.TryRead(out var pending)) Reject(pending);
        await _stop.CancelAsync().ConfigureAwait(false);

        var completed = await Task.WhenAny(
                _worker,
                Task.Delay(ShutdownJoinTimeout))
            .ConfigureAwait(false);
        if (ReferenceEquals(completed, _worker))
        {
            await _worker.ConfigureAwait(false);
            _stop.Dispose();
            return;
        }

        _ = _worker.ContinueWith(
            static (_, state) =>
                ((CancellationTokenSource)state!).Dispose(),
            _stop,
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private async ValueTask RunAsync(CancellationToken cancellationToken)
    {
        try
        {
            await foreach (var work in _queue.Reader.ReadAllAsync(cancellationToken)
                               .ConfigureAwait(false))
                try
                {
                    await work.Dispatch(cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                    when (cancellationToken.IsCancellationRequested)
                {
                }
                catch (Exception exception)
                {
                    Report(exception);
                }
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
        }
        finally
        {
            Interlocked.Exchange(ref _stopped, 1);
            _queue.Writer.TryComplete();
            while (_queue.Reader.TryRead(out var pending)) Reject(pending);
        }
    }

    private void Reject(DispatchWork work)
    {
        try
        {
            work.Reject();
        }
        catch (Exception exception)
        {
            Report(exception);
        }
    }

    private void Report(Exception exception)
    {
        try
        {
            _errorSink.ReportRuntimeTaskException(_name, exception);
        }
        catch
        {
        }
    }

    private sealed record DispatchWork(
        Func<CancellationToken, ValueTask> Dispatch,
        Action Reject);
}

internal sealed class ZLinkChannelReplyGate
{
    private readonly object _gate = new();
    private bool _open = true;

    internal bool TryInvoke(Action reply)
    {
        ArgumentNullException.ThrowIfNull(reply);
        lock (_gate)
        {
            if (!_open)
                return false;
            reply();
            return true;
        }
    }

    internal void Close()
    {
        lock (_gate)
            _open = false;
    }
}
