// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal sealed class SpotNodeOptions
{
    private SpotNode? _owner;
    private int _pubSubHighWaterMark;
    private AutoHwmProfile _pubSubHwmProfile = AutoHwmProfile.Balanced;
    private int _routerHighWaterMark;
    private AutoHwmProfile _routerHwmProfile = AutoHwmProfile.Balanced;

    public SpotNodeMode Mode { get; init; } = SpotNodeMode.All;

    public AutoHwmProfile RouterHwmProfile
    {
        get => _routerHwmProfile;
        set
        {
            _routerHwmProfile = value;
            _owner?.SetRouterHighWaterMarkProfile(value);
        }
    }

    public int RouterHighWaterMark
    {
        get => _routerHighWaterMark;
        set
        {
            _routerHighWaterMark = value;
            _owner?.SetRouterHighWaterMark(value);
        }
    }

    public AutoHwmProfile PubSubHwmProfile
    {
        get => _pubSubHwmProfile;
        set
        {
            _pubSubHwmProfile = value;
            _owner?.SetPubSubHighWaterMarkProfile(value);
        }
    }

    public int PubSubHighWaterMark
    {
        get => _pubSubHighWaterMark;
        set
        {
            _pubSubHighWaterMark = value;
            _owner?.SetPubSubHighWaterMark(value);
        }
    }

    internal void AttachOwner(SpotNode owner)
    {
        _owner = owner;
    }
}

internal sealed class SpotOptions
{
    private readonly Spot _spot;

    internal SpotOptions(Spot spot)
    {
        _spot = spot;
    }

    public TimeSpan? RequestTimeout
    {
        get => CommonSocketOptions.DecodeDuration(
            _spot.GetOption(SpotOption.RequestTimeout));
        set => _spot.SetOption(SpotOption.RequestTimeout,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public int SendHighWaterMark
    {
        get => _spot.GetSocketOption(SocketOptions.SndHwm);
        set => _spot.SetSocketOption(SocketOptions.SndHwm, value);
    }

    public int ReceiveHighWaterMark
    {
        get => _spot.GetSocketOption(SocketOptions.RcvHwm);
        set => _spot.SetSocketOption(SocketOptions.RcvHwm, value);
    }

    public int SendBufferSize
    {
        get => _spot.GetSocketOption(SocketOptions.SndBuf);
        set => _spot.SetSocketOption(SocketOptions.SndBuf, value);
    }

    public int ReceiveBufferSize
    {
        get => _spot.GetSocketOption(SocketOptions.RcvBuf);
        set => _spot.SetSocketOption(SocketOptions.RcvBuf, value);
    }

    public TimeSpan? SendTimeout
    {
        get => CommonSocketOptions.DecodeDuration(
            _spot.GetSocketOption(SocketOptions.SndTimeo));
        set => _spot.SetSocketOption(SocketOptions.SndTimeo,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? ReceiveTimeout
    {
        get => CommonSocketOptions.DecodeDuration(
            _spot.GetSocketOption(SocketOptions.RcvTimeo));
        set => _spot.SetSocketOption(SocketOptions.RcvTimeo,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }

    public TimeSpan? Linger
    {
        get => CommonSocketOptions.DecodeDuration(
            _spot.GetSocketOption(SocketOptions.Linger));
        set => _spot.SetSocketOption(SocketOptions.Linger,
            CommonSocketOptions.EncodeDuration(value, nameof(value)));
    }
}