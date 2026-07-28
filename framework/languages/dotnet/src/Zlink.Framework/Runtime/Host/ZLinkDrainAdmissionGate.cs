namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkDrainAdmissionGate
{
    private readonly object _gate = new();
    private int _acceptedActorAdmissions;
    private TaskCompletionSource? _actorAdmissionsDrained;
    private int _draining;
    private int _sealed;

    public bool IsDraining => Volatile.Read(ref _draining) != 0;

    public bool BeginDrain()
    {
        lock (_gate)
        {
            if (_draining != 0) return false;
            Volatile.Write(ref _draining, 1);
            return true;
        }
    }

    public bool IsSealed => Volatile.Read(ref _sealed) != 0;

    public void Seal() => Volatile.Write(ref _sealed, 1);

    public void Reset()
    {
        lock (_gate)
        {
            if (_acceptedActorAdmissions != 0)
                throw new InvalidOperationException(
                    "The drain admission gate cannot reset while actor admissions are active.");
            _actorAdmissionsDrained = null;
            Volatile.Write(ref _sealed, 0);
            Volatile.Write(ref _draining, 0);
        }
    }

    public bool TryEnterActorAdmission(out ActorAdmissionLease lease)
    {
        lock (_gate)
        {
            if (_draining != 0)
            {
                lease = new ActorAdmissionLease(null);
                return false;
            }
            _acceptedActorAdmissions++;
            lease = new ActorAdmissionLease(this);
            return true;
        }
    }

    public Task WaitForAcceptedActorAdmissionsAsync(CancellationToken cancellationToken)
    {
        lock (_gate)
        {
            if (_acceptedActorAdmissions == 0) return Task.CompletedTask;
            var pending = (_actorAdmissionsDrained ??= new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously)).Task;
            return pending.WaitAsync(cancellationToken);
        }
    }

    public void RequireSpotAdmission()
    {
        if (IsDraining)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.Rejected,
                "The framework runtime is draining and does not accept new SPOT assignments.",
                ZLinkRetryAdvice.DoNotRetry);
    }

    public void RequireActorAdmission()
    {
        if (IsDraining)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.Rejected,
                "The framework runtime is draining and does not accept new actor assignments.",
                ZLinkRetryAdvice.DoNotRetry);
    }

    private void ExitActorAdmission()
    {
        TaskCompletionSource? drained = null;
        lock (_gate)
        {
            if (--_acceptedActorAdmissions < 0)
                throw new InvalidOperationException("Actor admission lease count became negative.");
            if (_acceptedActorAdmissions == 0)
            {
                drained = _actorAdmissionsDrained;
                _actorAdmissionsDrained = null;
            }
        }
        drained?.TrySetResult();
    }

    public sealed class ActorAdmissionLease(ZLinkDrainAdmissionGate? owner) : IDisposable
    {
        private ZLinkDrainAdmissionGate? _owner = owner;

        public void Dispose() => Interlocked.Exchange(ref _owner, null)?.ExitActorAdmission();
    }
}
