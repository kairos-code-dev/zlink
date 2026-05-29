// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
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
    private IntPtr _handle;
    private readonly Dictionary<string, DealerSocket> _channelDealers =
        new(StringComparer.Ordinal);
    private readonly HashSet<Spot> _spots = new();
    private readonly object _spotsGate = new();
    private Action? _sendReadyHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;
    internal SpotNodeOptions Options { get; }
    internal SpotNodePublisherOptions PublisherOptions { get; }
    internal SpotNodeSubscriberOptions SubscriberOptions { get; }

    public SpotNode(Context context)
        : this(context, null)
    {
    }

    public SpotNode(Context context, SpotNodeMode mode)
        : this(context, new SpotNodeOptions { Mode = mode })
    {
    }

    internal SpotNode(Context context, SpotNodeOptions? options)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        Options = options ?? new SpotNodeOptions();
        if (options == null)
        {
            _handle = NativeMethods.zlink_spot_node_new(context.Handle,
                IntPtr.Zero);
        }
        else
        {
            ZlinkSpotNodeOptions nativeOptions = new()
            {
                Mode = Options.Mode
            };
            _handle = NativeMethods.zlink_spot_node_new(context.Handle,
                ref nativeOptions);
        }
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        PublisherOptions = new SpotNodePublisherOptions(this);
        SubscriberOptions = new SpotNodeSubscriberOptions(this);
        Options.AttachOwner(this);
        if (options != null)
            ApplyOptions(Options);
    }

    internal IntPtr Handle => _handle;

    public void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        byte[] routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                int rc = NativeMethods.zlink_set_routing_id(_handle,
                    (IntPtr)routingIdPtr, (nuint)routingIdBytes.Length);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }
    }

    public RoutingId RoutingId
    {
        get
        {
            EnsureNotDisposed();
            int rc = NativeMethods.zlink_get_routing_id(_handle,
                out ZlinkRoutingId routingId);
            ZlinkException.ThrowConfigIfError(rc);
            return RoutingId.From(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    public Spot CreateSpot()
    {
        EnsureNotDisposed();
        Spot spot = new(this);
        RegisterSpot(spot);
        return spot;
    }

    ISpot ISpotNode.CreateSpot()
    {
        return CreateSpot();
    }

    public Spot EntrySpot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_entry_spot(_handle,
            out IntPtr spotHandle);
        ZlinkException.ThrowConfigIfError(rc);
        Spot spot = new(this, spotHandle, ownsHandle: true);
        RegisterSpot(spot);
        return spot;
    }

    ISpot ISpotNode.EntrySpot()
    {
        return EntrySpot();
    }

    public Spot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = spotRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_spot_get_or_new(_handle,
            ref nativeRid, out IntPtr spotHandle, out uint createdValue);
        ZlinkException.ThrowConfigIfError(rc);
        if (spotHandle == IntPtr.Zero)
        {
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        }

        created = createdValue != 0;
        Spot spot = new(this, spotHandle, ownsHandle: true);
        RegisterSpot(spot);
        return spot;
    }

    ISpot ISpotNode.GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        return GetOrCreateSpot(spotRid, out created);
    }

    public Spot? SpotLookup(RoutingId spotRid)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeRid = spotRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_spot_lookup(_handle,
            ref nativeRid, out IntPtr spotHandle);
        try
        {
            ZlinkException.ThrowConfigIfError(rc);
        }
        catch (ZlinkConfigException error)
            when (error.Result == ZlinkConfigException.ErrorCode.NotFound)
        {
            return null;
        }
        if (spotHandle == IntPtr.Zero)
            return null;
        Spot spot = new(this, spotHandle, ownsHandle: true);
        RegisterSpot(spot);
        return spot;
    }

    ISpot? ISpotNode.SpotLookup(RoutingId spotRid)
    {
        return SpotLookup(spotRid);
    }

    public void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false)
    {
        BoundaryValidation.ValidateFixedUtf8(certPath, nameof(certPath));
        BoundaryValidation.ValidateFixedUtf8(keyPath, nameof(keyPath));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_server(_handle, certPath, keyPath,
            requireClientCert ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        BoundaryValidation.ValidateFixedUtf8(caCertPath, nameof(caCertPath));
        BoundaryValidation.ValidateFixedUtf8(hostname, nameof(hostname));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_client(_handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    private static unsafe void WriteFixedString(string value, byte* destination,
        int capacity)
    {
        byte[] encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length >= capacity)
        {
            throw new ArgumentOutOfRangeException(nameof(value),
                "UTF-8 value exceeds native fixed buffer capacity.");
        }

        for (int i = 0; i < capacity; i++)
            destination[i] = 0;
        for (int i = 0; i < encoded.Length; i++)
            destination[i] = encoded[i];
    }

}
