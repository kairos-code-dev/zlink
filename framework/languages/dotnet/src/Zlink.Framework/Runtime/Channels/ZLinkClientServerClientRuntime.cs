using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkClientServerClientRuntime : IAsyncDisposable
{
    private readonly string _channelName;
    private readonly IZLinkChannelBackendAdapter _adapter;
    private readonly IZLinkMonitoringBackendAdapter _monitoring;
    private readonly IZLinkBackendContext _context;
    private readonly IZLinkSocketConfig _socketConfig;
    private readonly TimeSpan _requestTimeout;
    private readonly CancellationToken _stopToken;
    private readonly object _gate = new();
    private readonly Dictionary<string, Connection> _connections =
        new(StringComparer.Ordinal);
    private readonly List<Task> _retired = [];

    internal ZLinkClientServerClientRuntime(
        string channelName,
        IZLinkChannelBackendAdapter adapter,
        IZLinkMonitoringBackendAdapter monitoring,
        IZLinkBackendContext context,
        IZLinkSocketConfig socketConfig,
        TimeSpan requestTimeout,
        CancellationToken stopToken)
    {
        _channelName = channelName;
        _adapter = adapter;
        _monitoring = monitoring;
        _context = context;
        _socketConfig = socketConfig;
        _requestTimeout = requestTimeout;
        _stopToken = stopToken;
    }

    internal void AddManual(string endpoint) =>
        AddOrReplace(
            $"manual:{endpoint}",
            endpoint,
            expected: null);

    internal void RemoveManual(string endpoint) =>
        Remove($"manual:{endpoint}");

    internal void AddLocal(
        string endpoint,
        ZLinkClientServerServerIdentity identity)
    {
        var snapshot = identity.Read();
        var key =
            $"local:{identity.ServerRid.ToHex()}:{identity.LifecycleGeneration}";
        AddOrReplace(
            key,
            endpoint,
            LocalDescriptor(identity, endpoint, snapshot));
        identity.SnapshotChanged += changed =>
        {
            lock (_gate)
                if (_connections.TryGetValue(key, out var connection))
                    connection.Update(LocalDescriptor(
                        identity,
                        endpoint,
                        changed));
        };
    }

    private ZLinkClientServerServerDescriptor LocalDescriptor(
        ZLinkClientServerServerIdentity identity,
        string endpoint,
        ZLinkClientServerServerIdentity.Snapshot snapshot) =>
        new(
                _channelName,
                identity.ServerRid,
                identity.LifecycleGeneration,
                snapshot.Revision,
                endpoint,
                snapshot.Weight,
                snapshot.State,
                identity.SecurityIdentity,
                "process-local",
                1,
                default);

    internal void ReplaceAutomatic(
        IReadOnlyList<ZLinkClientServerServerDescriptor> descriptors)
    {
        var desired = descriptors.ToDictionary(
            static row => $"auto:{row.ServerRid.ToHex()}:{row.LifecycleGeneration}",
            StringComparer.Ordinal);
        var successors = descriptors.ToDictionary(
            static row => row.ServerRid.ToHex(),
            static row => $"auto:{row.ServerRid.ToHex()}:{row.LifecycleGeneration}",
            StringComparer.Ordinal);
        string[] obsolete;
        lock (_gate)
            obsolete = _connections.Keys
                .Where(static key => key.StartsWith("auto:", StringComparison.Ordinal))
                .Where(key => !desired.ContainsKey(key))
                .ToArray();

        // Start successors before removing the previous lifecycle. Ready
        // publication is fenced inside each connection.
        foreach (var (key, descriptor) in desired)
            AddOrReplace(key, descriptor.Endpoint, descriptor);
        foreach (var key in obsolete)
        {
            var retainForSuccessor = false;
            lock (_gate)
            {
                if (_connections.TryGetValue(key, out var obsoleteConnection)
                    && obsoleteConnection.ExpectedServerRidHex is { } rid
                    && successors.TryGetValue(rid, out var successorKey)
                    && _connections.TryGetValue(
                        successorKey,
                        out var successor))
                    retainForSuccessor = !successor.AdmissionCompleted;
            }
            if (!retainForSuccessor)
                Remove(key);
        }
    }

    internal async ValueTask<ZLinkSubmitResult> SendAsync(
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var target = await WaitForReadyAsync(cancellationToken)
            .ConfigureAwait(false);
        if (target is null)
        {
            ZLinkMessageParts.DisposeAll(parts);
            return new ZLinkSubmitResult(ZLinkSubmitStatus.TargetNotFound);
        }
        return await target.Submitter.SubmitAsync(
                parts,
                pending => target.Socket.Send(pending, SendFlags.DontWait),
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestAsync(
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var target = await WaitForReadyAsync(cancellationToken)
            .ConfigureAwait(false);
        if (target is null)
        {
            ZLinkMessageParts.DisposeAll(parts);
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestTargetNotFound,
                $"ClientServer channel '{_channelName}' has no ready server.");
        }
        return await ZLinkRawRequestSubmitter.SubmitAsync(
                target.Submitter,
                parts,
                (pending, callback, nativeTimeout) => target.Socket.Request(
                    pending,
                    callback,
                    SendFlags.DontWait,
                    nativeTimeout),
                timeout,
                $"ClientServer request failed for '{_channelName}': {{0}}.",
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal int ReadyCount
    {
        get
        {
            lock (_gate)
                return DistinctConnections().Count(static value => value.Ready);
        }
    }

    internal int AdmissionCompletedCount
    {
        get
        {
            lock (_gate)
                return DistinctConnections().Count(
                    static value => value.AdmissionCompleted);
        }
    }

    internal int ConnectionIntentCount
    {
        get { lock (_gate) return _connections.Count; }
    }

    internal int PhysicalConnectionCount
    {
        get { lock (_gate) return DistinctConnections().Count(); }
    }

    internal long LivenessAckCount
    {
        get
        {
            lock (_gate)
                return DistinctConnections().Sum(
                    static connection => connection.LivenessAckCount);
        }
    }

    internal long ReceivedLivenessProbeCount
    {
        get
        {
            lock (_gate)
                return DistinctConnections().Sum(
                    static connection =>
                        connection.ReceivedLivenessProbeCount);
        }
    }

    internal long SentLivenessProbeCount
    {
        get
        {
            lock (_gate)
                return DistinctConnections().Sum(
                    static connection =>
                        connection.SentLivenessProbeCount);
        }
    }

    internal string AdmissionDiagnostics
    {
        get
        {
            lock (_gate)
                return string.Join(
                    "; ",
                    _connections.Select(
                        static entry =>
                            $"{entry.Key}={entry.Value.Diagnostics}"));
        }
    }

    internal IZLinkBackendSocket GetMonitoringSocket()
    {
        lock (_gate)
            return _connections.Values.FirstOrDefault()?.Socket
                   ?? throw new InvalidOperationException(
                       $"ClientServer client '{_channelName}' has no connection intent to monitor.");
    }

    public async ValueTask DisposeAsync()
    {
        Connection[] values;
        Task[] retired;
        lock (_gate)
        {
            values = DistinctConnections().ToArray();
            _connections.Clear();
            retired = _retired.ToArray();
            _retired.Clear();
        }
        foreach (var value in values)
            await value.DisposeAsync().ConfigureAwait(false);
        await Task.WhenAll(retired).ConfigureAwait(false);
    }

    private void AddOrReplace(
        string key,
        string endpoint,
        ZLinkClientServerServerDescriptor? expected)
    {
        Connection? previous = null;
        var retirePrevious = false;
        Connection created;
        lock (_gate)
        {
            if (_connections.TryGetValue(key, out var existing)
                && existing.Matches(endpoint, expected))
            {
                existing.Update(expected);
                return;
            }
            previous = existing;
            created = new Connection(
                _channelName,
                endpoint,
                expected,
                _adapter.CreateDealerSocket(_context),
                _monitoring,
                _socketConfig,
                _requestTimeout,
                _stopToken,
                OnAdmitted);
            _connections[key] = created;
            retirePrevious = previous is not null
                && !IsReferenced(previous);
        }
        if (retirePrevious)
            Retire(previous!);
        created.Start();
    }

    private void Remove(string key)
    {
        Connection? removed;
        lock (_gate)
        {
            if (!_connections.Remove(key, out removed))
                return;
            if (IsReferenced(removed))
                return;
        }
        Retire(removed);
    }

    private void Retire(Connection connection)
    {
        var task = connection.DisposeAsync().AsTask();
        lock (_gate)
        {
            _retired.RemoveAll(static candidate => candidate.IsCompleted);
            _retired.Add(task);
        }
    }

    private Connection? SelectReady()
    {
        lock (_gate)
        {
            var ready = DistinctConnections()
                .Where(static value => value.Ready && value.Weight > 0)
                .ToArray();
            var total = ZLinkWeightedSelector.Sum(
                ready,
                static value => value.Weight);
            if (total <= 0)
                return null;
            foreach (var connection in ready)
                connection.SelectionCurrent = checked(
                    connection.SelectionCurrent + connection.Weight);
            var selected = ready
                .OrderByDescending(
                    static value => value.SelectionCurrent)
                .First();
            selected.SelectionCurrent = checked(
                selected.SelectionCurrent - total);
            return selected;
        }
    }

    private IEnumerable<Connection> DistinctConnections() =>
        _connections.Values.Distinct(
            (IEqualityComparer<Connection>)ReferenceEqualityComparer.Instance);

    private bool IsReferenced(Connection connection) =>
        _connections.Values.Any(
            candidate => ReferenceEquals(candidate, connection));

    private void OnAdmitted(
        Connection admitted,
        string identity)
    {
        Connection? duplicate = null;
        lock (_gate)
        {
            if (!IsReferenced(admitted))
                return;
            var canonical = DistinctConnections().FirstOrDefault(
                candidate => !ReferenceEquals(candidate, admitted)
                    && StringComparer.Ordinal.Equals(
                        candidate.AdmittedIdentity,
                        identity));
            if (canonical is null)
                return;

            canonical.MergeExpected(admitted.Expected);
            foreach (var key in _connections
                         .Where(entry => ReferenceEquals(entry.Value, admitted))
                         .Select(static entry => entry.Key)
                         .ToArray())
                _connections[key] = canonical;
            duplicate = admitted;
        }
        if (duplicate is not null)
            Retire(duplicate);
    }

    private async ValueTask<Connection?> WaitForReadyAsync(
        CancellationToken cancellationToken)
    {
        var deadline = DateTime.UtcNow
            + (_requestTimeout < TimeSpan.FromSeconds(5)
                ? _requestTimeout
                : TimeSpan.FromSeconds(5));
        while (true)
        {
            if (SelectReady() is { } ready)
                return ready;
            if (DateTime.UtcNow >= deadline)
                return null;
            await Task.Delay(
                    TimeSpan.FromMilliseconds(5),
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private sealed class Connection : IAsyncDisposable
    {
        private readonly string _channelName;
        private readonly string _endpoint;
        private readonly TimeSpan _admissionTimeout;
        private readonly CancellationToken _stopToken;
        private readonly uint _normalizedEffectiveMaxMessageBytes;
        private readonly object _gate = new();
        private readonly IZLinkBackendSocketMonitor _monitor;
        private ZLinkClientServerServerDescriptor? _expected;
        private bool _disposed;
        private bool _admissionStarted;
        private bool _admissionCompleted;
        private bool _ready;
        private bool _rejected;
        private int _weight;
        private string _diagnostics = "configured";
        private string? _admittedIdentity;
        private ZLinkClientServerControlProtocol.Admission? _currentAdmission;
        private readonly CancellationTokenSource _admissionStop;
        private Task? _admissionTask;
        private readonly List<Task> _admissionTasks = [];
        private Task? _controlTask;
        private Task? _livenessTask;
        private Task? _reconnectTask;
        private bool _reconnectInProgress;
        private readonly Action<Connection, string> _onAdmitted;
        private ulong _nextProbeId = 1;
        private ulong? _outstandingProbeId;
        private DateTimeOffset _peerDeadline;
        private long _livenessAckCount;
        private long _receivedLivenessProbeCount;
        private long _sentLivenessProbeCount;
        private ulong _physicalGeneration = 1;
        private ulong _admissionAttempt;

        internal Connection(
            string channelName,
            string endpoint,
            ZLinkClientServerServerDescriptor? expected,
            IZLinkBackendDealerSocket socket,
            IZLinkMonitoringBackendAdapter monitoring,
            IZLinkSocketConfig socketConfig,
            TimeSpan requestTimeout,
            CancellationToken stopToken,
            Action<Connection, string> onAdmitted)
        {
            _channelName = channelName;
            _endpoint = endpoint;
            _expected = expected;
            _admissionTimeout = socketConfig.ConnectTimeout
                ?? TimeSpan.FromSeconds(1);
            _stopToken = stopToken;
            _onAdmitted = onAdmitted;
            _admissionStop =
                CancellationTokenSource.CreateLinkedTokenSource(stopToken);
            _normalizedEffectiveMaxMessageBytes =
                ZLinkClientServerControlProtocol.NormalizeMaximumMessageBytes(
                    socketConfig.MaxMessageSize);
            Socket = socket;
            Socket.SetChannelName(channelName);
            Socket.SetRoutingId(RoutingId.From($"csc-{Guid.NewGuid():N}"));
            ZLinkChannelBundleFactory.ApplySocketConfig(Socket, socketConfig);
            Socket.SetProbe(false);
            Submitter = new ZLinkAsyncSubmitter(
                Socket.OnSendReady,
                requestTimeout,
                stopToken,
                ZLinkAsyncSubmitter.ResolvePendingCapacity(
                    socketConfig.SendHighWaterMark));
            _monitor = monitoring.OpenSocketMonitor(Socket);
            _monitor.OnEvent(OnMonitorEvent);
        }

        internal IZLinkBackendDealerSocket Socket { get; }
        internal ZLinkAsyncSubmitter Submitter { get; }
        internal bool Ready { get { lock (_gate) return _ready && !_disposed; } }
        internal bool AdmissionCompleted
        {
            get { lock (_gate) return _admissionCompleted; }
        }
        internal int Weight { get { lock (_gate) return _weight; } }
        internal long SelectionCurrent { get; set; }
        internal string Diagnostics
        {
            get { lock (_gate) return _diagnostics; }
        }
        internal string? ExpectedServerRidHex
        {
            get { lock (_gate) return _expected?.ServerRid.ToHex(); }
        }
        internal string? AdmittedIdentity
        {
            get { lock (_gate) return _admittedIdentity; }
        }
        internal ZLinkClientServerServerDescriptor? Expected
        {
            get { lock (_gate) return _expected; }
        }
        internal long LivenessAckCount =>
            Interlocked.Read(ref _livenessAckCount);
        internal long ReceivedLivenessProbeCount =>
            Interlocked.Read(ref _receivedLivenessProbeCount);
        internal long SentLivenessProbeCount =>
            Interlocked.Read(ref _sentLivenessProbeCount);

        internal bool Matches(
            string endpoint,
            ZLinkClientServerServerDescriptor? expected)
        {
            lock (_gate)
                return StringComparer.Ordinal.Equals(_endpoint, endpoint)
                    && !_rejected
                    && (_expected is null && expected is null
                        || _expected is not null
                        && expected is not null
                        && _expected.ServerRid == expected.ServerRid
                        && _expected.LifecycleGeneration
                        == expected.LifecycleGeneration
                        && StringComparer.Ordinal.Equals(
                            _expected.SecurityIdentity,
                            expected.SecurityIdentity));
        }

        internal void Update(ZLinkClientServerServerDescriptor? expected)
        {
            lock (_gate)
            {
                _expected = expected;
                if (expected is not null)
                {
                    _weight = expected.Weight;
                    if (expected.State != ZLinkFrameworkRuntimeState.Serving
                        || expected.Weight <= 0)
                        _ready = false;
                    else if (_admissionCompleted)
                        _ready = true;
                }
            }
        }

        internal void MergeExpected(
            ZLinkClientServerServerDescriptor? expected)
        {
            if (expected is null)
                return;
            lock (_gate)
            {
                if (_admittedIdentity is not null
                    && !StringComparer.Ordinal.Equals(
                        _admittedIdentity,
                        IdentityOf(
                            expected.ServerRid,
                            expected.LifecycleGeneration)))
                    return;
                _expected = expected;
                _weight = expected.Weight;
                _ready = _admissionCompleted
                    && expected.State == ZLinkFrameworkRuntimeState.Serving
                    && expected.Weight > 0;
            }
        }

        internal void Start()
        {
            Socket.Connect(_endpoint);
            // The monitor normally starts admission. This delayed fallback
            // covers a connection that became ready before the monitor
            // subscriber observed its first event without submitting a native
            // request against a pipe that is still being established.
            _ = RetryAdmissionAsync();
        }

        public async ValueTask DisposeAsync()
        {
            lock (_gate)
            {
                if (_disposed) return;
                _disposed = true;
                _ready = false;
            }
            await _admissionStop.CancelAsync().ConfigureAwait(false);
            Task[] admissionTasks;
            lock (_gate) admissionTasks = _admissionTasks.ToArray();
            foreach (var admissionTask in admissionTasks)
                await IgnoreCancellationAsync(admissionTask)
                    .ConfigureAwait(false);
            if (_controlTask is not null)
                await IgnoreCancellationAsync(_controlTask).ConfigureAwait(false);
            if (_livenessTask is not null)
                await IgnoreCancellationAsync(_livenessTask).ConfigureAwait(false);
            if (_reconnectTask is not null)
                await IgnoreCancellationAsync(_reconnectTask).ConfigureAwait(false);
            try
            {
                Socket.Disconnect(_endpoint);
            }
            catch
            {
            }
            await _monitor.DisposeAsync().ConfigureAwait(false);
            await Submitter.DisposeAsync().ConfigureAwait(false);
            await Socket.DisposeAsync().ConfigureAwait(false);
            _admissionStop.Dispose();
        }

        private static async Task IgnoreCancellationAsync(Task task)
        {
            try
            {
                await task.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        private void OnMonitorEvent(ZLinkBackendSocketMonitorEvent value)
        {
            switch (value.NativeEvent)
            {
                case ZLinkSocketNativeEventType.ConnectionReady:
                    lock (_gate)
                    {
                        if (_disposed)
                            return;
                        if (_expected is { } expected
                            && (value.RoutingId is not { } actual
                                || actual != expected.ServerRid))
                        {
                            _ready = false;
                            _rejected = true;
                            _admissionCompleted = true;
                            return;
                        }
                    }
                    TryStartAdmission();
                    break;
                case ZLinkSocketNativeEventType.Disconnected:
                case ZLinkSocketNativeEventType.Closed:
                case ZLinkSocketNativeEventType.HandshakeFailedNoDetail:
                case ZLinkSocketNativeEventType.HandshakeFailedProtocol:
                case ZLinkSocketNativeEventType.HandshakeFailedAuth:
                    lock (_gate)
                    {
                        if (_reconnectInProgress)
                            _ready = false;
                        else
                            FencePhysicalConnection("transport:disconnected");
                    }
                    break;
            }
        }

        private void TryStartAdmission()
        {
            ulong physicalGeneration;
            ulong attempt;
            lock (_gate)
            {
                if (_disposed || _admissionStarted)
                    return;
                _admissionStarted = true;
                physicalGeneration = _physicalGeneration;
                attempt = ++_admissionAttempt;
                _admissionTask = RunAdmissionAsync(
                    physicalGeneration,
                    attempt,
                    _admissionStop.Token);
                _admissionTasks.RemoveAll(
                    static candidate => candidate.IsCompleted);
                _admissionTasks.Add(_admissionTask);
            }
        }

        private async Task RunAdmissionAsync(
            ulong physicalGeneration,
            ulong attempt,
            CancellationToken cancellationToken)
        {
            var retry = false;
            try
            {
                var hello = ZLinkClientServerControlProtocol.EncodeHello(
                    new ZLinkClientServerControlProtocol.Hello(
                        _channelName,
                        ZLinkTransportSecurityIdentity.Plaintext,
                        _normalizedEffectiveMaxMessageBytes));
                IReadOnlyList<Message> reply;
                try
                {
                    reply = await Socket.RequestAsync(
                            hello,
                            _admissionTimeout,
                            cancellationToken)
                        .ConfigureAwait(false);
                }
                catch
                {
                    hello.Dispose();
                    throw;
                }
                try
                {
                    _ = ApplyAdmission(
                        reply,
                        physicalGeneration,
                        attempt);
                }
                finally
                {
                    ZLinkMessageParts.DisposeAll(reply);
                }
            }
            catch (OperationCanceledException)
                when (cancellationToken.IsCancellationRequested)
            {
            }
            catch (Exception exception)
            {
                lock (_gate)
                {
                    if (!IsCurrentAttempt(physicalGeneration, attempt))
                        return;
                    _ready = false;
                    _admissionCompleted = false;
                    _diagnostics =
                        $"request:{exception.GetType().Name}";
                    retry = !_disposed;
                }
            }
            finally
            {
                lock (_gate)
                    if (IsCurrentAttempt(physicalGeneration, attempt))
                        _admissionStarted = false;
            }
            if (retry)
                _ = RetryAdmissionAsync();
        }

        private async Task RetryAdmissionAsync()
        {
            try
            {
                await Task.Delay(
                        TimeSpan.FromMilliseconds(100),
                        _admissionStop.Token)
                    .ConfigureAwait(false);
                TryStartAdmission();
            }
            catch (OperationCanceledException)
            {
            }
        }

        private bool ApplyAdmission(
            IReadOnlyList<Message> reply,
            ulong physicalGeneration,
            ulong attempt)
        {
            ZLinkClientServerServerDescriptor? expected;
            string? admittedIdentity = null;
            lock (_gate) expected = _expected;
            try
            {
                lock (_gate)
                {
                    if (!IsCurrentAttempt(physicalGeneration, attempt))
                        return false;
                    _admissionCompleted = true;
                }
                if (ZLinkClientServerControlProtocol.TryDecodeReject(
                        reply,
                        out _)
                    || !ZLinkClientServerControlProtocol.TryDecodeAdmission(
                        reply,
                        out var admission)
                    || admission is null
                    || !StringComparer.Ordinal.Equals(
                        admission.ChannelName,
                        _channelName)
                    || admission.LifecycleGeneration == 0
                    || admission.Weight is < 0 or > ZLinkSocketConfig.MaximumPeerWeight
                    || admission.NormalizedEffectiveMaxMessageBytes == 0
                    || admission.NormalizedEffectiveMaxMessageBytes
                    > _normalizedEffectiveMaxMessageBytes
                    || expected is not null
                    && (admission.ServerRid != expected.ServerRid
                        || admission.LifecycleGeneration
                        != expected.LifecycleGeneration
                        || admission.DescriptorRevision
                        != expected.DescriptorRevision
                        || admission.Weight != expected.Weight
                        || admission.State != expected.State
                        || !StringComparer.Ordinal.Equals(
                            admission.AdvertisedEndpoint,
                            expected.Endpoint)
                        || !StringComparer.Ordinal.Equals(
                            admission.SecurityIdentity,
                            expected.SecurityIdentity)))
                {
                    lock (_gate)
                    {
                        if (!IsCurrentAttempt(physicalGeneration, attempt))
                            return false;
                        _ready = false;
                        _rejected = true;
                        _diagnostics = reply.Count == 0
                            ? "invalid:empty"
                            : $"invalid:{Convert.ToHexString(
                                reply[0].AsReadOnlyMemory().Span)}";
                    }
                    return false;
                }
                lock (_gate)
                {
                    if (IsCurrentAttempt(physicalGeneration, attempt))
                    {
                        _weight = admission.Weight;
                        _ready =
                            admission.State
                            == ZLinkFrameworkRuntimeState.Serving
                            && _weight > 0;
                        _admittedIdentity = IdentityOf(
                            admission.ServerRid,
                            admission.LifecycleGeneration);
                        _currentAdmission = admission;
                        _peerDeadline =
                            DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
                        admittedIdentity = _admittedIdentity;
                        _diagnostics = "ready";
                        _controlTask ??=
                            RunControlLoopAsync(_admissionStop.Token);
                        _livenessTask ??=
                            RunLivenessLoopAsync(_admissionStop.Token);
                    }
                    else
                    {
                        return false;
                    }
                }
            }
            catch
            {
                lock (_gate)
                {
                    if (!IsCurrentAttempt(physicalGeneration, attempt))
                        return false;
                    _ready = false;
                    _diagnostics = "invalid:exception";
                }
                return false;
            }
            if (admittedIdentity is not null)
                _onAdmitted(this, admittedIdentity);
            return true;
        }

        private async Task RunControlLoopAsync(
            CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    using var received = Socket.Recv(RecvFlags.DontWait);
                    if (received is null)
                    {
                        await Task.Delay(
                                TimeSpan.FromMilliseconds(10),
                                cancellationToken)
                            .ConfigureAwait(false);
                        continue;
                    }
                    if (ZLinkClientServerControlProtocol.TryDecodeLivenessProbe(
                            received.Parts,
                            out var probeId))
                    {
                        Interlocked.Increment(
                            ref _receivedLivenessProbeCount);
                        var ack =
                            ZLinkClientServerControlProtocol.EncodeLivenessAck(
                                probeId);
                        if (received.RequestSeq is not null)
                            ReplyOwned(received, ack);
                        else
                            SendOwned(ack);
                        continue;
                    }
                    if (ZLinkClientServerControlProtocol.TryDecodeUpdate(
                            received.Parts,
                            out var update)
                        && update is not null)
                    {
                        ApplyUpdate(update);
                        continue;
                    }
                    if (ZLinkClientServerControlProtocol.TryDecodeLivenessAck(
                            received.Parts,
                            out var ackId))
                    {
                        lock (_gate)
                        {
                            if (_outstandingProbeId != ackId)
                                continue;
                            _outstandingProbeId = null;
                            _peerDeadline =
                                DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
                            if (_currentAdmission is
                                {
                                    State: ZLinkFrameworkRuntimeState.Serving,
                                    Weight: > 0
                                })
                                _ready = true;
                        }
                        Interlocked.Increment(ref _livenessAckCount);
                        continue;
                    }
                    var restartReason =
                        ZLinkClientServerControlProtocol.IsControl(received.Parts)
                            ? "protocol:pushed-control"
                            : "protocol:unsolicited-application";
                    // Release native receive parts before disconnecting the
                    // socket that produced them.
                    received.Dispose();
                    RestartPhysicalConnection(restartReason);
                }
                catch (OperationCanceledException)
                    when (cancellationToken.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception exception)
                {
                    lock (_gate)
                        _diagnostics =
                            $"control:{exception.GetType().Name}:{exception.Message}";
                    RestartPhysicalConnection(
                        $"control:{exception.GetType().Name}");
                    await Task.Delay(
                            TimeSpan.FromMilliseconds(10),
                            cancellationToken)
                        .ConfigureAwait(false);
                }
            }
        }

        private async Task RunLivenessLoopAsync(
            CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(
                        TimeSpan.FromSeconds(5),
                        cancellationToken)
                    .ConfigureAwait(false);
                ulong probeId;
                ulong physicalGeneration;
                var timedOut = false;
                lock (_gate)
                {
                    if (_disposed)
                        return;
                    if (DateTimeOffset.UtcNow >= _peerDeadline)
                    {
                        timedOut = true;
                    }
                    _outstandingProbeId ??= AllocateProbeId();
                    probeId = _outstandingProbeId.Value;
                    physicalGeneration = _physicalGeneration;
                }
                if (timedOut)
                {
                    RestartPhysicalConnection("liveness:timeout");
                    continue;
                }
                var probe =
                    ZLinkClientServerControlProtocol.EncodeLivenessProbe(
                        probeId);
                try
                {
                    if (Socket.Request(
                            probe,
                            (result, reply) =>
                            {
                                try
                                {
                                    if (result != RequestResult.Ok
                                        || !ZLinkClientServerControlProtocol
                                            .TryDecodeLivenessAck(
                                                reply,
                                                out var ackId))
                                        return;
                                    lock (_gate)
                                    {
                                        if (_physicalGeneration
                                                != physicalGeneration
                                            || _outstandingProbeId != ackId)
                                            return;
                                        _outstandingProbeId = null;
                                        _peerDeadline =
                                            DateTimeOffset.UtcNow
                                            + TimeSpan.FromSeconds(15);
                                        if (_currentAdmission is
                                            {
                                                State:
                                                ZLinkFrameworkRuntimeState
                                                    .Serving,
                                                Weight: > 0
                                            })
                                            _ready = true;
                                    }
                                    Interlocked.Increment(
                                        ref _livenessAckCount);
                                }
                                finally
                                {
                                    ZLinkMessageParts.DisposeAll(reply);
                                }
                            },
                            SendFlags.DontWait,
                            TimeSpan.FromSeconds(15)))
                    {
                        Interlocked.Increment(
                            ref _sentLivenessProbeCount);
                        continue;
                    }
                }
                catch
                {
                }
                probe.Dispose();
            }
        }

        private bool IsCurrentAttempt(
            ulong physicalGeneration,
            ulong attempt) =>
            !_disposed
            && _physicalGeneration == physicalGeneration
            && _admissionAttempt == attempt;

        private void FencePhysicalConnection(string diagnostics)
        {
            _physicalGeneration++;
            _admissionAttempt++;
            _admissionStarted = false;
            _admissionCompleted = false;
            _ready = false;
            _currentAdmission = null;
            _outstandingProbeId = null;
            _diagnostics = diagnostics;
        }

        private void RestartPhysicalConnection(string diagnostics)
        {
            ulong physicalGeneration;
            ulong admissionAttempt;
            lock (_gate)
            {
                if (_disposed || _reconnectInProgress)
                    return;
                _reconnectInProgress = true;
                FencePhysicalConnection(diagnostics);
                physicalGeneration = _physicalGeneration;
                admissionAttempt = _admissionAttempt;
            }
            try
            {
                Socket.Disconnect(_endpoint);
            }
            catch
            {
            }
            _reconnectTask = ReconnectAsync(
                physicalGeneration,
                admissionAttempt);
        }

        private async Task ReconnectAsync(
            ulong physicalGeneration,
            ulong admissionAttempt)
        {
            try
            {
                // Disconnect completion is asynchronous at the transport
                // layer. Give its monitor event a chance to retire the old
                // pipe before registering the same endpoint again.
                await Task.Delay(
                        TimeSpan.FromMilliseconds(25),
                        _admissionStop.Token)
                    .ConfigureAwait(false);
                lock (_gate)
                    if (_disposed
                        || _physicalGeneration != physicalGeneration
                        || _admissionAttempt != admissionAttempt)
                        return;
                Socket.Connect(_endpoint);
            }
            catch (OperationCanceledException)
                when (_admissionStop.IsCancellationRequested)
            {
                return;
            }
            catch
            {
            }
            finally
            {
                lock (_gate)
                    _reconnectInProgress = false;
            }
            _ = RetryAdmissionAsync();
        }

        private void ApplyUpdate(
            ZLinkClientServerControlProtocol.Admission update)
        {
            lock (_gate)
            {
                var current = _currentAdmission;
                if (current is null
                    || update.ChannelName != current.ChannelName
                    || update.ServerRid != current.ServerRid
                    || update.LifecycleGeneration
                    != current.LifecycleGeneration
                    || update.SecurityIdentity != current.SecurityIdentity
                    || update.AdvertisedEndpoint
                    != current.AdvertisedEndpoint
                    || update.NormalizedEffectiveMaxMessageBytes
                    != current.NormalizedEffectiveMaxMessageBytes)
                {
                    _ready = false;
                    _diagnostics = "invalid:update-identity";
                    return;
                }
                if (update.DescriptorRevision < current.DescriptorRevision)
                    return;
                if (update.DescriptorRevision == current.DescriptorRevision)
                {
                    if (update != current)
                    {
                        _ready = false;
                        _diagnostics = "invalid:update-conflict";
                    }
                    return;
                }
                _currentAdmission = update;
                _weight = update.Weight;
                _ready = update.State == ZLinkFrameworkRuntimeState.Serving
                    && update.Weight > 0;
                _diagnostics = _ready ? "ready" : "update:not-ready";
            }
        }

        private bool SendOwned(Message message)
        {
            try
            {
                if (Socket.Send(message, SendFlags.DontWait))
                    return true;
            }
            catch
            {
            }
            message.Dispose();
            return false;
        }

        private void ReplyOwned(
            Received received,
            Message message)
        {
            try
            {
                if (Socket.Reply(received, message))
                    return;
            }
            catch
            {
            }
            message.Dispose();
        }

        private ulong AllocateProbeId()
        {
            var result = _nextProbeId;
            _nextProbeId = result == long.MaxValue ? 1 : result + 1;
            return result;
        }

        private static string IdentityOf(
            RoutingId serverRid,
            ulong lifecycleGeneration) =>
            $"{serverRid.ToHex()}:{lifecycleGeneration}";
    }
}
