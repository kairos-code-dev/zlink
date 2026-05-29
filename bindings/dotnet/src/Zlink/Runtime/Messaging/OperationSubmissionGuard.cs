// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal struct OperationSubmissionGuard
{
    private bool _submitted;

    internal void EnsureNotSubmitted()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }

    internal void MarkSubmitted()
    {
        EnsureNotSubmitted();
        _submitted = true;
    }
}
