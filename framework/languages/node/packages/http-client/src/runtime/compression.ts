/* SPDX-License-Identifier: MPL-2.0 */

import { gunzipSync, inflateSync, inflateRawSync } from 'node:zlib';
import { ZLinkFrameworkException, ZLinkFrameworkErrorKind } from '@zlink-systems/framework';

/**
 * Wrapper-controlled response decompression mirroring the C++ `compression.cpp`: gzip and deflate
 * are decoded, the decoded size is bounded by the configured body limit (enforced by zlib's
 * `maxOutputLength` so a malicious response cannot allocate past the limit before the check), and the
 * caller removes the `Content-Encoding` header afterwards. undici's `request` does not auto-decompress,
 * so streaming downloads are never transparently decoded. A malformed body raises `payloadDecodeFailed`;
 * exceeding the limit raises `requestFailed`.
 */
export function gunzip(input: Buffer, maxBytes: number): Buffer {
  return decode(() => gunzipSync(input, { maxOutputLength: maxBytes }));
}

export function inflateDeflate(input: Buffer, maxBytes: number): Buffer {
  // Detect a zlib-wrapped stream (CMF/FLG: method deflate, header a multiple of 31) vs raw deflate.
  const zlibWrapped =
    input.length >= 2 && (input[0] & 0x0f) === 8 && (((input[0] << 8) | input[1]) % 31) === 0;
  return decode(() =>
    zlibWrapped
      ? inflateSync(input, { maxOutputLength: maxBytes })
      : inflateRawSync(input, { maxOutputLength: maxBytes }),
  );
}

function decode(run: () => Buffer): Buffer {
  try {
    return run();
  } catch (cause) {
    // zlib throws a RangeError (ERR_BUFFER_TOO_LARGE) when output exceeds maxOutputLength.
    if (cause instanceof RangeError) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        'HTTP response compressed body exceeds maxResponseBodySize',
      );
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.PayloadDecodeFailed,
      'HTTP response compressed body is malformed',
      false,
      cause,
    );
  }
}
