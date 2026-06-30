// SPDX-License-Identifier: MPL-2.0

using System.Text;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    private readonly Dictionary<string, DealerSocket> _channelDealers =
        new(StringComparer.Ordinal);

    private readonly HashSet<Spot> _spots = new();
    private readonly object _spotsGate = new();
    private Action? _sendReadyHandler;
    private SynchronizationContext? _sendReadyHandlerContext;
    private NativeMethods.ZlinkSendReadyHandlerDelegate? _sendReadyHandlerNative;

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
        Context = context;
        Options = options ?? new SpotNodeOptions();
        if (options == null)
        {
            Handle = NativeMethods.zlink_spot_node_new(context.Handle,
                IntPtr.Zero);
        }
        else
        {
            ZlinkSpotNodeOptions nativeOptions = new()
            {
                Mode = Options.Mode
            };
            Handle = NativeMethods.zlink_spot_node_new(context.Handle,
                ref nativeOptions);
        }

        if (Handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        PublisherOptions = new SpotNodePublisherOptions(this);
        SubscriberOptions = new SpotNodeSubscriberOptions(this);
        Options.AttachOwner(this);
        if (options != null)
            ApplyOptions(Options);
    }

    internal Context Context { get; }
    internal SpotNodeOptions Options { get; }
    internal SpotNodePublisherOptions PublisherOptions { get; }
    internal SpotNodeSubscriberOptions SubscriberOptions { get; }

    internal IntPtr Handle { get; private set; }

    public void SetRoutingId(RoutingId routingId)
    {
        EnsureNotDisposed();
        var routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                var rc = NativeMethods.zlink_set_routing_id(Handle,
                    (IntPtr)routingIdPtr, (nuint)routingIdBytes.Length);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }
    }

    public void SetPublisherRoutingId(RoutingId routingId)
    {
        SetSpotPlaneRoutingId(routingId,
            static (handle, data, size) =>
                NativeMethods.zlink_spot_node_set_pub_routing_id(handle, data, size));
    }

    public void SetSubscriberRoutingId(RoutingId routingId)
    {
        SetSpotPlaneRoutingId(routingId,
            static (handle, data, size) =>
                NativeMethods.zlink_spot_node_set_sub_routing_id(handle, data, size));
    }

    public RoutingId RoutingId
    {
        get
        {
            EnsureNotDisposed();
            var rc = NativeMethods.zlink_get_routing_id(Handle,
                out var routingId);
            ZlinkException.ThrowConfigIfError(rc);
            return RoutingId.From(
                NativeHelpers.ReadRoutingId(ref routingId));
        }
    }

    ISpot ISpotNode.CreateSpot()
    {
        return CreateSpot();
    }

    ISpot ISpotNode.EntrySpot()
    {
        return EntrySpot();
    }

    ISpot ISpotNode.GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        return GetOrCreateSpot(spotRid, out created);
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
        var rc = NativeMethods.zlink_set_tls_server(Handle, certPath, keyPath,
            requireClientCert ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        BoundaryValidation.ValidateFixedUtf8(caCertPath, nameof(caCertPath));
        BoundaryValidation.ValidateFixedUtf8(hostname, nameof(hostname));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_set_tls_client(Handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    private void SetSpotPlaneRoutingId(
        RoutingId routingId,
        SpotNodeRoutingIdSetter setter)
    {
        EnsureNotDisposed();
        if (setter == null)
            throw new ArgumentNullException(nameof(setter));
        var routingIdBytes = routingId.ToByteArray();
        unsafe
        {
            fixed (byte* routingIdPtr = routingIdBytes)
            {
                var rc = setter(
                    Handle,
                    (IntPtr)routingIdPtr,
                    (nuint)routingIdBytes.Length);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }
    }

    public Spot CreateSpot()
    {
        EnsureNotDisposed();
        Spot spot = new(this);
        RegisterSpot(spot);
        return spot;
    }

    public Spot EntrySpot()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_entry_spot(Handle,
            out var spotHandle);
        ZlinkException.ThrowConfigIfError(rc);
        Spot spot = new(this, spotHandle, true);
        RegisterSpot(spot);
        return spot;
    }

    public Spot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        EnsureNotDisposed();
        var nativeRid = spotRid.ToNative();
        var rc = NativeMethods.zlink_spot_node_spot_get_or_new(Handle,
            ref nativeRid, out var spotHandle, out var createdValue);
        ZlinkException.ThrowConfigIfError(rc);
        if (spotHandle == IntPtr.Zero) throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());

        created = createdValue != 0;
        Spot spot = new(this, spotHandle, true);
        RegisterSpot(spot);
        return spot;
    }

    public Spot? SpotLookup(RoutingId spotRid)
    {
        EnsureNotDisposed();
        var nativeRid = spotRid.ToNative();
        var rc = NativeMethods.zlink_spot_node_spot_lookup(Handle,
            ref nativeRid, out var spotHandle);
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
        Spot spot = new(this, spotHandle, true);
        RegisterSpot(spot);
        return spot;
    }

    private static unsafe void WriteFixedString(string value, byte* destination,
        int capacity)
    {
        var encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length >= capacity)
            throw new ArgumentOutOfRangeException(nameof(value),
                "UTF-8 value exceeds native fixed buffer capacity.");

        for (var i = 0; i < capacity; i++)
            destination[i] = 0;
        for (var i = 0; i < encoded.Length; i++)
            destination[i] = encoded[i];
    }

    private delegate int SpotNodeRoutingIdSetter(
        IntPtr handle,
        IntPtr data,
        nuint size);
}