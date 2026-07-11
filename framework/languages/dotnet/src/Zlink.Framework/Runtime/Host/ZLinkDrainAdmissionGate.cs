namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkDrainAdmissionGate
{
    private int _draining;
    private int _sealed;

    public bool IsDraining => Volatile.Read(ref _draining) != 0;

    public bool BeginDrain() => Interlocked.Exchange(ref _draining, 1) == 0;

    public bool IsSealed => Volatile.Read(ref _sealed) != 0;

    public void Seal() => Volatile.Write(ref _sealed, 1);

    public void Reset()
    {
        Volatile.Write(ref _sealed, 0);
        Volatile.Write(ref _draining, 0);
    }

    public void RequireSpotAdmission()
    {
        if (IsDraining)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "The framework runtime is draining and does not accept new SPOT assignments.",
                false);
    }

    public void RequireActorAdmission()
    {
        if (IsDraining)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateRejected,
                "The framework runtime is draining and does not accept new actor assignments.",
                false);
    }
}
