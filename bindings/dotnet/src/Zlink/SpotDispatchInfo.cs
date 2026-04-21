// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink;

public sealed record SpotDispatchInfo(
    SpotDispatchEvent Event,
    SpotDispatchSubjectKind SubjectKind,
    IntPtr Subject);
