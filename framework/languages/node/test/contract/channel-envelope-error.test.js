'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');
const envelope = require('../../packages/framework/dist/runtime/channels/channel-envelope');

function readable(parts) {
  return parts.map((part) => typeof part.data === 'function'
    ? part
    : { data: () => Buffer.from(part) });
}

test('IMP-ND-03 channel Error preserves the public framework kind without retry hints', () => {
  const requestParts = envelope.encodeChannelEnvelopeParts(1, 'api', 'Lookup', { id: 'a' });
  const request = envelope.decodeChannelEnvelope(readable(requestParts));
  const replyParts = envelope.encodeChannelErrorReplyParts(
    request.header,
    new framework.ZLinkFrameworkException(
      framework.ZLinkFrameworkErrorKind.Unavailable,
      'route is converging'
    )
  );
  try {
    assert.throws(
      () => envelope.decodeChannelReply(readable(replyParts)),
      (error) => error instanceof framework.ZLinkFrameworkException
        && error.kind === framework.ZLinkFrameworkErrorKind.Unavailable
        && !('isRetriable' in error)
        && error.message === 'route is converging'
    );
  } finally {
    envelope.closeMessages(requestParts);
    envelope.closeMessages(replyParts);
  }
});

test('IMP-ND-03 channel Error maps a non-framework error to InternalFailure', () => {
  const requestParts = envelope.encodeChannelEnvelopeParts(1, 'api', 'Lookup', { id: 'a' });
  const request = envelope.decodeChannelEnvelope(readable(requestParts));
  const failure = new TypeError('bad handler input');
  const replyParts = envelope.encodeChannelErrorReplyParts(request.header, failure);
  try {
    assert.throws(
      () => envelope.decodeChannelReply(readable(replyParts)),
      (error) => error instanceof framework.ZLinkFrameworkException
        && error.kind === framework.ZLinkFrameworkErrorKind.InternalFailure
        && error.message === failure.message
    );
  } finally {
    envelope.closeMessages(requestParts);
    envelope.closeMessages(replyParts);
  }
});

test('IMP-ND-03 unknown channel content type fails before handler payload delivery', () => {
  const envelopeValue = {
    header: {
      formatMarker: envelope.ZLINK_CHANNEL_FORMAT_MARKER,
      kind: 1,
      channelName: 'api',
      messageName: 'Lookup',
      contentType: 'application/x-unknown',
      correlationId: 'unknown-content-type',
      deadline: null,
      topic: null,
      metadata: {}
    },
    payload: Buffer.from([0x01, 0x02, 0x03])
  };

  assert.throws(
      () => envelope.decodeChannelPayload(envelopeValue),
    (error) => error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.ProtocolError
      && /unsupported channel content type/.test(error.message)
  );
});
