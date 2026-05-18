using Systems.Zlink.Stream.Connector.Contracts;
using System.Collections.Concurrent;

namespace Systems.Zlink.Stream.Connector.Unity;

public enum ZlinkUnityCallbackOverflowPolicy
{
    DropNewest,
    DropOldest,
    Throw
}

public enum ZlinkUnityPausePolicy
{
    KeepConnected,
    CloseOnPause
}

public sealed class ZlinkUnityDispatchOptions
{
    public int MaxQueuedCallbacks { get; init; } = 1024;

    public ZlinkUnityCallbackOverflowPolicy OverflowPolicy { get; init; } =
        ZlinkUnityCallbackOverflowPolicy.Throw;
}

public sealed class ZlinkUnityStreamConnectorOptions
{
    public required ZlinkStreamConnectorOptions Connector { get; init; }

    public ZlinkUnityDispatchOptions Dispatch { get; init; } = new();

    public ZlinkUnityPausePolicy PausePolicy { get; init; } = ZlinkUnityPausePolicy.KeepConnected;
}

public sealed class ZlinkUnityCallbackDispatcher
{
    private readonly ConcurrentQueue<Func<ValueTask>> _callbacks = new();
    private readonly ZlinkUnityDispatchOptions _options;

    public ZlinkUnityCallbackDispatcher(ZlinkUnityDispatchOptions? options = null)
    {
        _options = options ?? new ZlinkUnityDispatchOptions();
    }

    public int Count => _callbacks.Count;

    public void Enqueue(Func<ValueTask> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);

        if (_callbacks.Count >= _options.MaxQueuedCallbacks)
        {
            switch (_options.OverflowPolicy)
            {
                case ZlinkUnityCallbackOverflowPolicy.DropNewest:
                    return;
                case ZlinkUnityCallbackOverflowPolicy.DropOldest:
                    _callbacks.TryDequeue(out _);
                    break;
                case ZlinkUnityCallbackOverflowPolicy.Throw:
                default:
                    throw new InvalidOperationException("Unity callback queue is full.");
            }
        }

        _callbacks.Enqueue(callback);
    }

    public async ValueTask PumpAsync(CancellationToken cancellationToken = default)
    {
        while (_callbacks.TryDequeue(out var callback))
        {
            cancellationToken.ThrowIfCancellationRequested();
            await callback().ConfigureAwait(false);
        }
    }
}

#if UNITY_5_3_OR_NEWER
public sealed class ZlinkStreamConnectorBehaviour : UnityEngine.MonoBehaviour
#else
public sealed class ZlinkStreamConnectorBehaviour : IAsyncDisposable
#endif
{
    private ZlinkUnityStreamConnectorOptions? _options;
    private IZlinkStreamConnector? _connector;
    private ZlinkUnityCallbackDispatcher? _dispatcher;

    public IZlinkStreamConnector? Connector => _connector;

    public ZlinkUnityCallbackDispatcher? Dispatcher => _dispatcher;

    public bool IsConnected => _connector?.IsConnected == true;

    public void Configure(ZlinkUnityStreamConnectorOptions options)
    {
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _dispatcher = new ZlinkUnityCallbackDispatcher(options.Dispatch);
    }

    public async ValueTask ConnectAsync(CancellationToken cancellationToken = default)
    {
        if (_options is null)
        {
            throw new InvalidOperationException("Unity stream connector options are not configured.");
        }

        _connector = ZlinkStreamConnectorFactory.Create(_options.Connector);
        await _connector.ConnectAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken = default)
    {
        if (_connector is not null)
        {
            await _connector.CloseAsync(cancellationToken).ConfigureAwait(false);
            _connector = null;
        }
    }

    public void EnqueueCallback(Func<ValueTask> callback)
    {
        if (_dispatcher is null)
        {
            throw new InvalidOperationException("Unity callback dispatcher is not configured.");
        }

        _dispatcher.Enqueue(callback);
    }

    public ValueTask PumpCallbacksAsync(CancellationToken cancellationToken = default)
        => _dispatcher?.PumpAsync(cancellationToken) ?? ValueTask.CompletedTask;

#if UNITY_5_3_OR_NEWER
    private async void Update()
    {
        if (_dispatcher is not null)
        {
            await _dispatcher.PumpAsync();
        }
    }

    private async void OnApplicationPause(bool pause)
    {
        if (pause && _options?.PausePolicy == ZlinkUnityPausePolicy.CloseOnPause)
        {
            await CloseAsync();
        }
    }

    private async void OnDestroy()
    {
        await CloseAsync();
    }
#else
    public async ValueTask DisposeAsync()
    {
        await CloseAsync().ConfigureAwait(false);
    }
#endif
}
