import * as zlink from '@zlink-systems/zlink';

const ctx = zlink.createContext();
const routingId = zlink.RoutingId.from(Buffer.from('id'));
const peerRoutingId = zlink.RoutingId.from(Buffer.from('peer'));
zlink.SocketType.Pair;
const typedSpotDispatchEvent: zlink.SpotDispatchEvent = zlink.SpotDispatchEvent.TimerReadable;
const typedSpotDispatchSubjectKind: zlink.SpotDispatchSubjectKind = zlink.SpotDispatchSubjectKind.Timer;
const typedPollEvent: zlink.PollEventFlagValue = zlink.PollEventFlag.PollIn;
const typedRidPolicy: zlink.RidDuplicatePolicyValue = zlink.RidDuplicatePolicy.Reject;
const typedSendFlag: zlink.SendFlags = zlink.SendFlags.None;
const typedRecvFlag: zlink.RecvFlags = zlink.RecvFlags.DontWait;
const typedSubmitResult: zlink.SubmitResult = zlink.SubmitResult.Ok;
const typedRequestResult: zlink.RequestResult = zlink.RequestResult.Ok;
void typedSpotDispatchEvent;
void typedSpotDispatchSubjectKind;
void typedPollEvent;
void typedRidPolicy;
void typedSendFlag;
void typedRecvFlag;
void typedSubmitResult;
void typedRequestResult;
ctx.shutdown();
ctx.options.ioThreads = ctx.options.ioThreads;
ctx.options.maxSockets = ctx.options.maxSockets;
ctx.options.maxMsgSize = ctx.options.maxMsgSize;
ctx.options.threadPriority = ctx.options.threadPriority;
ctx.options.threadSchedulingPolicy = ctx.options.threadSchedulingPolicy;
ctx.options.threadNamePrefix = 'zlink';
ctx.options.threadNamePrefix;
ctx.options.blocky = ctx.options.blocky;
ctx.options.addThreadAffinity(0);
ctx.options.removeThreadAffinity(0);
ctx.options.socketLimit;
ctx.options.msgTSize;
ctx.recalculateAutoHwm();

const monitorHandler: zlink.SocketMonitorHandler = () => {};
void monitorHandler;

const pub = zlink.createPubSocket(ctx);
const baseSocket: zlink.BaseSocket = pub;
void baseSocket;
pub.publish('topic').message('ok').submit();
pub.setSendReadyHandler(() => {});
pub.setTlsServer('cert', 'key');
pub.setTlsClient('ca', 'host');
pub.options.verbose = true;

const sub = zlink.createSubSocket(ctx);
sub.setSubscription('topic');
sub.subscriptionAt(0);
const subTopicMessage = new zlink.TopicMessage();
sub.subscribe(subTopicMessage);
sub.setTlsServer('cert', 'key');
sub.setTlsClient('ca', 'host');
sub.options.topicsCount;

const dealerReceived = new zlink.Received();
const dealer = zlink.createDealerSocket(ctx);
dealer.send().message('ok').submit();
dealer.recv(dealerReceived);
dealer.setSendReadyHandler(() => {});
dealer.setRoutingId(routingId);
dealer.getRoutingId();
dealer.setTlsServer('cert', 'key');
dealer.setTlsClient('ca', 'host');
dealer.options.probe = true;

const routerReceived = new zlink.Received();
const router = zlink.createRouterSocket(ctx);
router.recv(routerReceived);
router.setSendReadyHandler(() => {});
router.setRoutingId(routingId);
router.getRoutingId();
router.setTlsServer('cert', 'key');
router.setTlsClient('ca', 'host');
router.send(routingId).message('ok').submit();
router.reply(routingId, 1n).message('ok').submit();
router.options.mandatory = true;
router.options.setConnectRoutingId(peerRoutingId);

const stream = zlink.createStreamSocket(ctx);
stream.send(routingId).message('ok').submit();
const streamReceived = new zlink.Received();
stream.recv(streamReceived);
stream.setPacketHandler((sourceRid, header, body) => {
  sourceRid.toString();
  header.data();
  body.data();
});
stream.setSendReadyHandler(() => {});
stream.setRoutingId(routingId);
stream.getRoutingId();
stream.setTlsServer('cert', 'key');
stream.setTlsClient('ca', 'host');
stream.options.notify = true;

const xpub = zlink.createXPubSocket(ctx);
xpub.publish('topic').message('ok').submit();
const xpubSubscriptionEvent = new zlink.SubscriptionEvent();
xpub.receiveSubscriptionEvent(xpubSubscriptionEvent);
xpub.setSendReadyHandler(() => {});
xpub.options.verbose = true;
xpub.options.verboser = true;
xpub.options.noDrop = true;
xpub.options.manual = true;
xpub.options.manualLastValue = true;
xpub.options.welcomeMessage();
xpub.options.setWelcomeMessage('welcome');

// @ts-expect-error option facades are created by their owning socket/context.
new zlink.CommonSocketOptions(pub);
// @ts-expect-error option facades are created by their owning socket/context.
new zlink.ContextOptions(ctx);

const spotNode = zlink.createSpotNode(ctx);
spotNode.setTlsServer('cert', 'key');
spotNode.setTlsClient('ca', 'host');
spotNode.setRoutingId(routingId);
spotNode.setPublisherRoutingId(routingId);
spotNode.setSubscriberRoutingId(routingId);
spotNode.routingId;
const spot = spotNode.createSpot();
const actor = spotNode.createActor('typed-actor');
const actorRef = actor.ref();
actorRef.generation;
spotNode.actorLookup('typed-actor');
spotNode.remoteActorGetRef(routingId, 'typed-actor').submit((_result) => {});
spotNode.sendToActor(actorRef).message('actor-send').message('actor-send-2').submit();
spotNode.requestToActor(actorRef).message('actor-request').submit((_result, _parts) => {});
spotNode.joinActor(actorRef, routingId, routingId).message('join').submit((_result, _parts) => {});
spotNode.joinActorEntrySpot(actorRef, routingId, 'entry-join').message('part').timeout(1000).submit((_result, _parts) => {});
// @ts-expect-error Entry Spot joins require an explicit request message.
spotNode.joinActorEntrySpot(actorRef, routingId);
spotNode.leaveActor(actorRef, routingId).submit((_result, _parts) => {});
spotNode.spots();
spotNode.actors();
const entrySpot = spotNode.entrySpot();
const lookedUpSpot = spotNode.spotLookup(entrySpot.routingId);
const room = spotNode.getOrCreateSpot(routingId);
room.spot.close();
const created: boolean = room.created;
void created;
stream.bindActor(routingId, actorRef).submit((_result, _parts) => {});
stream.unbindActor(routingId, 'typed-actor').submit((_result, _parts) => {});
stream.sendBoundActor(routingId, 'typed-actor').message('payload').submit();
spot.setRoutingId(routingId);
spot.routingId;
spot.publish('topic').message('ok').submit();
const spotTopicStorage = new zlink.TopicMessage();
spot.subscribe(spotTopicStorage, zlink.RecvFlags.DontWait);
const spotRoutedStorage = new zlink.Received();
spot.recvRouted(spotRoutedStorage, zlink.RecvFlags.DontWait);
spot.sendToChannel('svc').message('ok').submit();
spot.sendToSpot(routingId, routingId).message(Buffer.from('ok')).flags(zlink.SendFlags.DontWait).submit();
spot.requestToSpot(routingId, routingId).message(Buffer.from('ok')).flags(zlink.SendFlags.DontWait).timeout(1000).submit((_result, _parts) => {});
spot.requestToChannel('svc').message('ok').submit();
spot.setSubscription('topic');
spot.subscriptionAt(0);
spot.unsetSubscription('topic');
const spotTopicMessage = new zlink.TopicMessage();
spot.subscribe(spotTopicMessage);
spot.setSendReadyHandler(() => {});
spot.setDispatchHandler((info) => {
  info.actorRef?.generation;
  info.timer?.stop();
  info.recvActorPart(zlink.RecvFlags.DontWait);
});
spot.recvActorLifecycle(zlink.RecvFlags.DontWait)?.info.currentActor.actorId;
const actorJoin = spot.recvActorJoin(zlink.RecvFlags.DontWait);
if (actorJoin) {
  spot.replyActorJoin(actorJoin, 0).message('ok').submit();
}
spot.actors();
const routeBridge = spotNode.createRouteBridge();
routeBridge.attachRouterChannel('mesh', router, {
  capabilities: zlink.SpotRouteBridgeEndpointCapabilities.RouteOnly
});
routeBridge.send('svc', routingId, routingId).message('payload').submit();
routeBridge.request('svc', routingId, routingId).message('payload').timeout(1000).submit((_result, _parts) => {});
const routeBridgeHandled: boolean = routeBridge.handleRouterReceived('mesh', routingId, 1n, [Buffer.from('payload')]);
void routeBridgeHandled;
routeBridge.close();
const spotPublisher = spotNode.createPublisher();
spotPublisher.publish('topic').message('payload').submit();
spotPublisher.close();

const serviceMeshNode = zlink.createMeshNode(ctx, { meshName: 'typecheck.mesh' });
const meshPublisher = serviceMeshNode.createPublisher();
const meshPublishResult: Promise<zlink.MeshPublishResult> = meshPublisher.publishAsync(
  'mesh.channel',
  'topic',
  'payload',
  undefined,
  new AbortController().signal
);
void meshPublishResult;
meshPublisher.close();
serviceMeshNode.close();

const monitor = pub.monitorOpen();
monitor.recv();
monitor.onEvent(() => {});
monitor.status();
monitor.close();

const poller = zlink.createPoller();
const pollEvents = zlink.createPollEvents(1);
poller.add(pub, [zlink.PollEventFlag.PollIn], 1);
poller.modify(pub, [zlink.PollEventFlag.PollIn, zlink.PollEventFlag.PollOut]);
const pollCount = poller.wait(pollEvents, 0);
if (pollCount > 0) {
  pollEvents.sourceKind(0);
  pollEvents.slot(0);
  pollEvents.revents(0);
  pollEvents.fd(0);
}
poller.remove(pub);
poller.size;
pollEvents.close();
poller.close();

const thread = zlink.createThread(() => {});
thread.join();
const counter = zlink.createAtomicCounter(1);
counter.set(counter.inc());
counter.dec();
counter.value();
counter.close();

const diagnosticMessage = zlink.Message.from(Buffer.from('diagnostic'));
const constructedMessage = zlink.Message.from(Buffer.from('constructed'));
const allocatedMessage = zlink.Message.allocate(16);
const allocatedMessageAlias = zlink.Message.allocate(16);
diagnosticMessage.data();
diagnosticMessage.toBytes();
diagnosticMessage.copy();
diagnosticMessage.size();
diagnosticMessage.isEmpty();
diagnosticMessage.copyTo(Buffer.alloc(16));
diagnosticMessage.tryCopyTo(Buffer.alloc(16));
diagnosticMessage.getString();
diagnosticMessage.getProperty('Socket-Type');
diagnosticMessage.refCount();
diagnosticMessage.close();
constructedMessage.data();
allocatedMessage.data().fill(0);
allocatedMessageAlias.data().fill(0);
constructedMessage.close();

// @ts-expect-error stream recv requires caller-provided output storage.
stream.recv();
// @ts-expect-error send payloads must go through operation builders.
dealer.send('direct-payload');
// @ts-expect-error publish payloads must go through operation builders.
pub.publish('topic', 'direct-payload');
// @ts-expect-error routed send payloads must go through operation builders.
router.send(routingId, 'direct-payload');
// @ts-expect-error stream send payloads must go through operation builders.
stream.send(routingId, 'direct-payload');
// @ts-expect-error request payloads must go through operation builders.
dealer.request('direct-payload');
// @ts-expect-error reply payloads must go through operation builders.
router.reply(routingId, 1n, 'direct-payload');
// @ts-expect-error spot publish payloads must go through operation builders.
spot.publish('topic', 'direct-payload');
// @ts-expect-error channel send payloads must go through operation builders.
spot.sendToChannel('svc', 'direct-payload');
// @ts-expect-error channel request payloads must go through operation builders.
spot.requestToChannel('svc', 'direct-payload');

spot.close();
spotNode.close();
stream.close();
router.close();
dealer.close();
sub.close();
xpub.close();
pub.close();
ctx.close();
