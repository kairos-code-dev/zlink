namespace Zlink.Framework.Runtime.Spots;

internal abstract class ZLinkSpotCall
{
    private readonly Func<
        string?,
        ZLinkMessage,
        string?,
        string?,
        TimeSpan,
        CancellationToken,
        ValueTask<ZLinkSpotCreateResult>> _submit;
    private string? _meshName;
    private ZLinkMessage _request = ZLinkMessage.Empty;
    private string? _placementProfile;
    private string? _affinityKey;
    private TimeSpan? _timeout;
    private bool _requestSet;
    private int _submitted;

    protected ZLinkSpotCall(
        Func<
            string?,
            ZLinkMessage,
            string?,
            string?,
            TimeSpan,
            CancellationToken,
            ValueTask<ZLinkSpotCreateResult>> submit)
    {
        _submit = submit;
    }

    protected void SetMesh(string value)
    {
        if (_meshName is not null) Duplicate("InMesh");
        _meshName = Required(value, nameof(value));
    }

    protected void SetRequest(ZLinkMessage value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (_requestSet) Duplicate("Request");
        _request = value;
        _requestSet = true;
    }

    protected void SetPlacementProfile(string value)
    {
        if (_placementProfile is not null) Duplicate("PlacementProfile");
        _placementProfile = Required(value, nameof(value));
    }

    protected void SetAffinityKey(string value)
    {
        if (_affinityKey is not null) Duplicate("AffinityKey");
        _affinityKey = Required(value, nameof(value));
    }

    protected void SetTimeout(TimeSpan value)
    {
        if (_timeout is not null) Duplicate("Timeout");
        if (value <= TimeSpan.Zero)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.InvalidConfiguration,
                "Timeout must be positive.");
        _timeout = value;
    }

    protected ValueTask<ZLinkSpotCreateResult> SubmitAsync(
        TimeSpan defaultTimeout,
        CancellationToken cancellationToken)
    {
        if (Interlocked.Exchange(ref _submitted, 1) != 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.AlreadySubmitted,
                "The User Spot call was already submitted.");
        return _submit(
            _meshName,
            _request,
            _placementProfile,
            _affinityKey,
            _timeout ?? defaultTimeout,
            cancellationToken);
    }

    private static string Required(string value, string parameter)
    {
        if (string.IsNullOrWhiteSpace(value)
            || System.Text.Encoding.UTF8.GetByteCount(value) > byte.MaxValue
            || value.Contains('\0'))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.InvalidConfiguration,
                $"{parameter} must be 1..255 UTF-8 bytes without NUL.");
        return value;
    }

    private static void Duplicate(string option) =>
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.InvalidConfiguration,
            $"{option} was already configured.");
}

internal sealed class ZLinkSpotCreateCall(
    TimeSpan defaultTimeout,
    Func<
        string?,
        ZLinkMessage,
        string?,
        string?,
        TimeSpan,
        CancellationToken,
        ValueTask<ZLinkSpotCreateResult>> submit)
    : ZLinkSpotCall(submit), IZLinkSpotCreateCall
{
    public IZLinkSpotCreateCall InMesh(string meshName)
    {
        SetMesh(meshName);
        return this;
    }

    public IZLinkSpotCreateCall Request(ZLinkMessage request)
    {
        SetRequest(request);
        return this;
    }

    public IZLinkSpotCreateCall Request<TRequest>(TRequest request)
    {
        SetRequest(ZLinkMessage.From(request));
        return this;
    }

    public IZLinkSpotCreateCall PlacementProfile(string placementProfile)
    {
        SetPlacementProfile(placementProfile);
        return this;
    }

    public IZLinkSpotCreateCall AffinityKey(string affinityKey)
    {
        SetAffinityKey(affinityKey);
        return this;
    }

    public IZLinkSpotCreateCall Timeout(TimeSpan timeout)
    {
        SetTimeout(timeout);
        return this;
    }

    public ValueTask<ZLinkSpotCreateResult> Async(
        CancellationToken cancellationToken = default) =>
        SubmitAsync(defaultTimeout, cancellationToken);
}

internal sealed class ZLinkSpotGetOrCreateCall(
    TimeSpan defaultTimeout,
    Func<
        string?,
        ZLinkMessage,
        string?,
        string?,
        TimeSpan,
        CancellationToken,
        ValueTask<ZLinkSpotCreateResult>> submit)
    : ZLinkSpotCall(submit), IZLinkSpotGetOrCreateCall
{
    public IZLinkSpotGetOrCreateCall InMesh(string meshName)
    {
        SetMesh(meshName);
        return this;
    }

    public IZLinkSpotGetOrCreateCall Request(ZLinkMessage request)
    {
        SetRequest(request);
        return this;
    }

    public IZLinkSpotGetOrCreateCall Request<TRequest>(TRequest request)
    {
        SetRequest(ZLinkMessage.From(request));
        return this;
    }

    public IZLinkSpotGetOrCreateCall PlacementProfile(string placementProfile)
    {
        SetPlacementProfile(placementProfile);
        return this;
    }

    public IZLinkSpotGetOrCreateCall AffinityKey(string affinityKey)
    {
        SetAffinityKey(affinityKey);
        return this;
    }

    public IZLinkSpotGetOrCreateCall Timeout(TimeSpan timeout)
    {
        SetTimeout(timeout);
        return this;
    }

    public ValueTask<ZLinkSpotCreateResult> Async(
        CancellationToken cancellationToken = default) =>
        SubmitAsync(defaultTimeout, cancellationToken);
}
