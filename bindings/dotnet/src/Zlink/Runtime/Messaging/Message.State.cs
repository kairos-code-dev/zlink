// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed partial class Message
{
    private ZlinkMsg _msg;
    private bool _valid;
    private int _knownSize = -1;
    private ManagedPayloadState? _managedPayload;
    private bool _pooled;

    [ThreadStatic]
    private static Message[]? t_pool;
    [ThreadStatic]
    private static int t_poolCount;
    private const int PoolCapacity = 256;

    internal Message(bool init)
    {
        if (init)
            Init();
    }
}
