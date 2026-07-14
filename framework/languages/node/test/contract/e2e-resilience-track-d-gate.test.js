const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../../e2e/ResilienceLifecycle');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('RL-D1 drives actual fanout through many subscribers', () => {
  const scenario = read('Client/Scenarios/rl-d1-high-fanout-scenario.ts');
  const provider = read('Server/Provider/provider-host-factory.ts');
  const consumer = read('Server/Consumer/consumer-host-factory.ts');
  const runner = read('run_e2e.sh');

  assert.match(provider, /addFanoutChannel\(ResilienceNames\.fanoutChannel\)/);
  assert.match(provider, /enablePublisher\(options\.fanoutEndpoint\)/);
  assert.match(consumer, /enableSubscriber\(\)/);
  assert.match(consumer, /addPublishHandler\(PacketNames\.loadEvent, LoadEventHandler\)/);
  assert.match(runner, /RL_D1_SUBSCRIBER_COUNT/);
  assert.match(runner, /--fanout-subscriber-urls/);
  assert.match(scenario, /options\.fanoutSubscriberUrls/);
  assert.match(scenario, /\/fanout\/publish/);
  assert.doesNotMatch(scenario, /\/profile\/request/);
});

test('RL-D4 channel replies preserve the canonical Error and Response wire headers', () => {
  const envelope = require('../../packages/framework/dist/runtime/channels/channel-envelope');
  const request = {
    formatMarker: 0xf2,
    kind: 1,
    channelName: 'profile',
    messageName: 'MissingProfileReq',
    contentType: 'application/json',
    correlationId: 'rl-d4-correlation',
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  };

  const errorHeader = JSON.parse(Buffer.from(
    envelope.encodeChannelErrorReplyParts(request, new Error('missing handler'))[0]
  ).toString('utf8'));
  assert.equal(errorHeader.kind, 5);
  assert.equal(errorHeader.errorCode, 'Error');
  assert.equal(errorHeader.errorMessage, 'missing handler');
  assert.equal(Object.hasOwn(errorHeader, 'status'), false);

  const responseHeader = JSON.parse(Buffer.from(
    envelope.encodeChannelReplyParts(request, { value: 'ok' })[0]
  ).toString('utf8'));
  assert.equal(responseHeader.kind, 2);
  assert.equal(Object.hasOwn(responseHeader, 'errorCode'), false);
  assert.equal(Object.hasOwn(responseHeader, 'errorMessage'), false);
  assert.equal(Object.hasOwn(responseHeader, 'status'), false);
});

test('RL-D4 checks decoded error code and message before a successful follow-up', () => {
  const scenario = read('Client/Scenarios/rl-d4-missing-request-handler-scenario.ts');
  assert.match(scenario, /failed\.failureType === 'Error'/);
  assert.match(scenario, /failed\.failureMessage\.includes\(/);
  assert.match(scenario, /followUp\.value === 'profile:fast'/);
});
