// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed class PairSocket : MessageSocketBase
{
    public PairSocket(Context context)
        : base(context, SocketType.Pair)
    {
    }
}
