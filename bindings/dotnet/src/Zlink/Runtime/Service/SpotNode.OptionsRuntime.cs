// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    public AutoHwmProfile RouterHwmProfile
    {
        get => Options.RouterHwmProfile;
        set => Options.RouterHwmProfile = value;
    }

    public int RouterHighWaterMark
    {
        get => Options.RouterHighWaterMark;
        set => Options.RouterHighWaterMark = value;
    }

    public AutoHwmProfile PubSubHwmProfile
    {
        get => Options.PubSubHwmProfile;
        set => Options.PubSubHwmProfile = value;
    }

    public int PubSubHighWaterMark
    {
        get => Options.PubSubHighWaterMark;
        set => Options.PubSubHighWaterMark = value;
    }

    public bool PublisherNoDrop
    {
        set => PublisherOptions.NoDrop = value;
    }

    public TimeSpan? PublisherSendTimeout
    {
        set => PublisherOptions.SendTimeout = value;
    }

    public int DispatchWorkersMin
    {
        get => GetAdmissionOption(SpotNodeOption.DispatchWorkersMin);
        set
        {
            if (value < 1)
                throw new ArgumentOutOfRangeException(nameof(value));
            SetAdmissionOption(SpotNodeOption.DispatchWorkersMin, value);
        }
    }

    public int DispatchWorkersMax
    {
        get => GetAdmissionOption(SpotNodeOption.DispatchWorkersMax);
        set
        {
            if (value < 1 || value < DispatchWorkersMin)
                throw new ArgumentOutOfRangeException(nameof(value));
            SetAdmissionOption(SpotNodeOption.DispatchWorkersMax, value);
        }
    }

    internal void SetOption(SpotNodeSocketRole role, SocketOptionKey<int> option,
        int value)
    {
        EnsureNotDisposed();
        unsafe
        {
            int local = value;
            int code = (int)option.Option;
            int rc = role switch
            {
                SpotNodeSocketRole.Node => NativeMethods.zlink_set_option(
                    _handle, code, new IntPtr(&local),
                    (nuint)sizeof(int)),
                SpotNodeSocketRole.Pub => (code & 0xFF00) == 0x3300
                    ? NativeMethods.zlink_set_pub_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int))
                    : NativeMethods.zlink_set_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int)),
                SpotNodeSocketRole.Sub => (code & 0xFF00) == 0x3400
                    ? NativeMethods.zlink_set_sub_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int))
                    : NativeMethods.zlink_set_option(_handle, code,
                        new IntPtr(&local), (nuint)sizeof(int)),
                _ => throw new ArgumentOutOfRangeException(nameof(role))
            };
            ZlinkException.ThrowConfigIfError(rc);
        }
    }

    internal void SetAdmissionOption(SpotNodeOption option, int value)
    {
        EnsureNotDisposed();
        unsafe
        {
            int local = value;
            int rc = NativeMethods.zlink_set_spot_node_option(_handle, option,
                new IntPtr(&local), (nuint)sizeof(int));
            ZlinkException.ThrowConfigIfError(rc);
        }
    }

    internal int GetAdmissionOption(SpotNodeOption option)
    {
        EnsureNotDisposed();
        unsafe
        {
            int value = 0;
            nuint size = (nuint)sizeof(int);
            int rc = NativeMethods.zlink_get_spot_node_option(_handle, option,
                new IntPtr(&value), ref size);
            ZlinkException.ThrowConfigIfError(rc);
            return value;
        }
    }

    internal void SetRouterHighWaterMark(int value)
    {
        SetAdmissionOption(SpotNodeOption.RouterHwm, value);
    }

    internal void SetPubSubHighWaterMark(int value)
    {
        SetAdmissionOption(SpotNodeOption.PubSubHwm, value);
    }

    internal void SetRouterHighWaterMarkProfile(AutoHwmProfile profile)
    {
        SetAdmissionOption(SpotNodeOption.RouterHwmProfile, (int)profile);
    }

    internal void SetPubSubHighWaterMarkProfile(AutoHwmProfile profile)
    {
        SetAdmissionOption(SpotNodeOption.PubSubHwmProfile, (int)profile);
    }

    private void ApplyOptions(SpotNodeOptions options)
    {
        SetRouterHighWaterMarkProfile(options.RouterHwmProfile);
        SetPubSubHighWaterMarkProfile(options.PubSubHwmProfile);
        if (options.RouterHighWaterMark > 0)
            SetRouterHighWaterMark(options.RouterHighWaterMark);
        if (options.PubSubHighWaterMark > 0)
            SetPubSubHighWaterMark(options.PubSubHighWaterMark);
    }
}
