// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal static class ActorContractValidation
{
    internal static void ValidateActorId(string actorId, string paramName)
    {
        if (actorId == null)
            throw new ArgumentNullException(paramName);
        if (actorId.IndexOf('\0') >= 0)
            throw new ArgumentException("Actor id must not contain NUL.",
                paramName);
        BoundaryValidation.ValidateFixedUtf8(actorId, paramName);
    }
}
