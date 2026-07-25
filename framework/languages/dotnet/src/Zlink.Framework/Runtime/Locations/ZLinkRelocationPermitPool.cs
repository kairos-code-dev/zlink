namespace Zlink.Framework.Runtime.Locations;

internal readonly record struct ZLinkRelocationPermitRequest(
    int OutboundUnits,
    int InboundUnits,
    int CaptureCallbacks,
    int RestoreCallbacks,
    long PayloadBytes,
    bool AllowOversizedPayload = false)
{
    internal static ZLinkRelocationPermitRequest Outbound(
        long payloadBytes,
        bool capture,
        bool allowOversizedPayload = false) =>
        new(
            OutboundUnits: 1,
            InboundUnits: 0,
            CaptureCallbacks: capture ? 1 : 0,
            RestoreCallbacks: 0,
            PayloadBytes: payloadBytes,
            AllowOversizedPayload: allowOversizedPayload);

    internal static ZLinkRelocationPermitRequest Inbound(
        long payloadBytes,
        bool restore,
        bool allowOversizedPayload = false) =>
        new(
            OutboundUnits: 0,
            InboundUnits: 1,
            CaptureCallbacks: 0,
            RestoreCallbacks: restore ? 1 : 0,
            PayloadBytes: payloadBytes,
            AllowOversizedPayload: allowOversizedPayload);
}

/// <summary>
/// Owns process-wide relocation admission as one atomic accounting domain.
/// A caller either receives every requested permit or leaves all counters unchanged.
/// </summary>
internal sealed class ZLinkRelocationPermitPool
{
    private readonly object _gate = new();
    private readonly int _maxOutboundUnits;
    private readonly int _maxInboundUnits;
    private readonly int _maxCaptureCallbacks;
    private readonly int _maxRestoreCallbacks;
    private readonly long _maxPayloadBytes;
    private int _outboundUnits;
    private int _inboundUnits;
    private int _captureCallbacks;
    private int _restoreCallbacks;
    private long _payloadBytes;
    private bool _oversizedPayloadActive;

    internal ZLinkRelocationPermitPool(ZLinkLocationOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        _maxOutboundUnits = Positive(
            options.MaxActiveOutboundRelocations,
            nameof(options.MaxActiveOutboundRelocations));
        _maxInboundUnits = Positive(
            options.MaxActiveInboundRelocations,
            nameof(options.MaxActiveInboundRelocations));
        _maxCaptureCallbacks = Positive(
            options.MaxConcurrentRelocationCaptures,
            nameof(options.MaxConcurrentRelocationCaptures));
        _maxRestoreCallbacks = Positive(
            options.MaxConcurrentRelocationRestores,
            nameof(options.MaxConcurrentRelocationRestores));
        _maxPayloadBytes = Positive(
            options.MaxRelocationPayloadInFlightBytes,
            nameof(options.MaxRelocationPayloadInFlightBytes));
    }

    internal bool TryAcquire(
        ZLinkRelocationPermitRequest request,
        out ZLinkRelocationPermitLease lease)
    {
        Validate(request);
        lock (_gate)
        {
            var oversized = request.PayloadBytes > _maxPayloadBytes;
            if (_outboundUnits > _maxOutboundUnits - request.OutboundUnits
                || _inboundUnits > _maxInboundUnits - request.InboundUnits
                || _captureCallbacks > _maxCaptureCallbacks - request.CaptureCallbacks
                || _restoreCallbacks > _maxRestoreCallbacks - request.RestoreCallbacks
                || !CanAdmitPayload(request, oversized))
            {
                lease = default;
                return false;
            }

            _outboundUnits += request.OutboundUnits;
            _inboundUnits += request.InboundUnits;
            _captureCallbacks += request.CaptureCallbacks;
            _restoreCallbacks += request.RestoreCallbacks;
            _payloadBytes = checked(_payloadBytes + request.PayloadBytes);
            _oversizedPayloadActive = oversized;
            lease = new ZLinkRelocationPermitLease(this, request);
            return true;
        }
    }

    internal ZLinkRelocationPermitSnapshot Snapshot()
    {
        lock (_gate)
            return new ZLinkRelocationPermitSnapshot(
                _outboundUnits,
                _inboundUnits,
                _captureCallbacks,
                _restoreCallbacks,
                _payloadBytes,
                _oversizedPayloadActive);
    }

    private bool CanAdmitPayload(
        ZLinkRelocationPermitRequest request,
        bool oversized)
    {
        if (oversized)
            return request.AllowOversizedPayload
                   && !_oversizedPayloadActive
                   && _payloadBytes == 0;
        return !_oversizedPayloadActive
               && _payloadBytes <= _maxPayloadBytes - request.PayloadBytes;
    }

    private void Release(ZLinkRelocationPermitRequest request)
    {
        lock (_gate)
        {
            _outboundUnits -= request.OutboundUnits;
            _inboundUnits -= request.InboundUnits;
            _captureCallbacks -= request.CaptureCallbacks;
            _restoreCallbacks -= request.RestoreCallbacks;
            _payloadBytes -= request.PayloadBytes;
            if (request.PayloadBytes > _maxPayloadBytes)
                _oversizedPayloadActive = false;

            if (_outboundUnits < 0
                || _inboundUnits < 0
                || _captureCallbacks < 0
                || _restoreCallbacks < 0
                || _payloadBytes < 0)
                throw new InvalidOperationException(
                    "Relocation permit accounting became negative.");
        }
    }

    private static void Validate(ZLinkRelocationPermitRequest request)
    {
        if (request.OutboundUnits < 0
            || request.InboundUnits < 0
            || request.CaptureCallbacks < 0
            || request.RestoreCallbacks < 0
            || request.PayloadBytes < 0)
            throw new ArgumentOutOfRangeException(
                nameof(request),
                "Relocation permit counts and bytes cannot be negative.");
        if (request.OutboundUnits == 0
            && request.InboundUnits == 0
            && request.CaptureCallbacks == 0
            && request.RestoreCallbacks == 0
            && request.PayloadBytes == 0)
            throw new ArgumentException(
                "A relocation permit request must reserve at least one resource.",
                nameof(request));
    }

    private static int Positive(int value, string name) =>
        value > 0
            ? value
            : throw new ArgumentOutOfRangeException(name, value, "The limit must be greater than zero.");

    private static long Positive(long value, string name) =>
        value > 0
            ? value
            : throw new ArgumentOutOfRangeException(name, value, "The limit must be greater than zero.");

    internal readonly struct ZLinkRelocationPermitLease : IDisposable
    {
        private readonly LeaseState? _state;

        internal ZLinkRelocationPermitLease(
            ZLinkRelocationPermitPool owner,
            ZLinkRelocationPermitRequest request)
        {
            _state = new LeaseState(owner, request);
        }

        public void Dispose() => _state?.Dispose();

        private sealed class LeaseState(
            ZLinkRelocationPermitPool owner,
            ZLinkRelocationPermitRequest request) : IDisposable
        {
            private int _disposed;

            public void Dispose()
            {
                if (Interlocked.Exchange(ref _disposed, 1) == 0)
                    owner.Release(request);
            }
        }
    }
}

internal readonly record struct ZLinkRelocationPermitSnapshot(
    int OutboundUnits,
    int InboundUnits,
    int CaptureCallbacks,
    int RestoreCallbacks,
    long PayloadBytes,
    bool OversizedPayloadActive);
