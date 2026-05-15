import * as zlink from '@zlink-systems/zlink';

const ctx = new zlink.Context();
const routingId = zlink.RoutingId.fromBytes(Buffer.from('id'));
const peerRoutingId = zlink.RoutingId.fromBytes(Buffer.from('peer'));
const AUTO_CONNECT_SPOT_MESH = 5;
const AUTO_CONNECT_ROUTE_MESH = 1;
const AUTO_CONNECT_CLIENT_SERVER = 2;
const AUTO_CONNECT_FANOUT = 4;
zlink.SocketType.Pair;
const typedAutoConnect: zlink.AutoConnectType = zlink.AutoConnectType.SpotMesh;
const typedSpotDispatchEvent: zlink.SpotDispatchEvent = zlink.SpotDispatchEvent.TimerReadable;
const typedSpotDispatchSubjectKind: zlink.SpotDispatchSubjectKind = zlink.SpotDispatchSubjectKind.Timer;
const typedPollEvent: zlink.PollEventFlagValue = zlink.PollEventFlag.PollIn;
const typedRidPolicy: zlink.RidDuplicatePolicyValue = zlink.RidDuplicatePolicy.Reject;
const typedSendFlag: zlink.SendFlags = zlink.SendFlags.None;
const typedRecvFlag: zlink.RecvFlags = zlink.RecvFlags.DontWait;
const typedSubmitResult: zlink.SubmitResult = zlink.SubmitResult.Ok;
const typedRequestResult: zlink.RequestResult = zlink.RequestResult.Ok;
void typedAutoConnect;
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

const registry = new zlink.Registry(ctx);
registry.setTlsServer('cert', 'key');
registry.setTlsClient('ca', 'host');

const discovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'surface-discovery');
discovery.setTlsClient('ca', 'host');
discovery.resolveSpot(routingId);
discovery.memberPeers();
const monitorHandler: zlink.SocketMonitorHandler = zlink.MonitorSocket.ignoreHandler;
void monitorHandler;

const pub = new zlink.PubSocket(ctx);
const baseSocket: zlink.BaseSocket = pub;
void baseSocket;
pub.publish('topic').message('ok').submit();
pub.onSendReady(() => {});
pub.attachDiscovery(new zlink.Discovery(ctx, AUTO_CONNECT_FANOUT, 'pub-service'));
pub.setTlsServer('cert', 'key');
pub.setTlsClient('ca', 'host');
pub.options.verbose = true;

const sub = new zlink.SubSocket(ctx);
sub.setSubscription('topic');
sub.subscriptionAt(0);
const subTopicMessage = new zlink.TopicMessage();
sub.subscribe(subTopicMessage);
sub.attachDiscovery(new zlink.Discovery(ctx, AUTO_CONNECT_FANOUT, 'sub-service'));
sub.setTlsServer('cert', 'key');
sub.setTlsClient('ca', 'host');
sub.options.topicsCount;

const dealerReceived = new zlink.Received();
const dealer = new zlink.DealerSocket(ctx);
dealer.send().message('ok').submit();
dealer.recv(dealerReceived);
dealer.onSendReady(() => {});
dealer.setRoutingId(routingId);
dealer.getRoutingId();
dealer.attachDiscovery(new zlink.Discovery(ctx, AUTO_CONNECT_CLIENT_SERVER, 'dealer-service'));
dealer.setTlsServer('cert', 'key');
dealer.setTlsClient('ca', 'host');
dealer.options.probe = true;

const routerReceived = new zlink.Received();
const router = new zlink.RouterSocket(ctx);
router.recv(routerReceived);
router.onSendReady(() => {});
router.setRoutingId(routingId);
router.getRoutingId();
router.attachDiscovery(new zlink.Discovery(ctx, AUTO_CONNECT_ROUTE_MESH, 'router-service'));
router.setTlsServer('cert', 'key');
router.setTlsClient('ca', 'host');
router.send(routingId).message('ok').submit();
router.reply(routingId, 1n).message('ok').submit();
router.options.mandatory = true;
router.options.setConnectRoutingId(peerRoutingId);

const stream = new zlink.StreamSocket(ctx);
stream.send(routingId).message('ok').submit();
const streamReceived = new zlink.Received();
stream.recv(streamReceived);
stream.onPacket((sourceRid, header, body) => {
  sourceRid.toString();
  header.data();
  body.data();
});
stream.onSendReady(() => {});
stream.setRoutingId(routingId);
stream.getRoutingId();
stream.setTlsServer('cert', 'key');
stream.setTlsClient('ca', 'host');
stream.options.notify = true;

const xpub = new zlink.XPubSocket(ctx);
xpub.publish('topic').message('ok').submit();
const xpubSubscriptionEvent = new zlink.SubscriptionEvent();
xpub.receiveSubscriptionEvent(xpubSubscriptionEvent);
xpub.onSendReady(() => {});
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

const spotNode = new zlink.SpotNode(ctx);
spotNode.setTlsServer('cert', 'key');
spotNode.setTlsClient('ca', 'host');
spotNode.setRoutingId(routingId);
spotNode.routingId;
const spot = spotNode.createSpot();
const actor = spotNode.createActor('typed-actor');
const actorRef = actor.ref();
actorRef.generation;
spotNode.actorLookup('typed-actor');
spotNode.remoteActorGetRef(routingId, 'typed-actor').submit((_result) => {});
spotNode.joinActor(actorRef, routingId, routingId).message('join').submit((_result, _parts) => {});
spotNode.leaveActor(actorRef, routingId).submit((_result, _parts) => {});
spotNode.spotsSnapshot();
spotNode.actorsSnapshot();
const entrySpot = spotNode.entrySpot();
const lookedUpSpot = spotNode.spotLookup(entrySpot.routingId);
stream.bindActor(routingId, actorRef).submit((_result, _parts) => {});
stream.unbindActor(routingId, 'typed-actor').submit((_result, _parts) => {});
stream.sendBoundActor(routingId, 'typed-actor').message('payload').submit();
spot.setRoutingId(routingId);
spot.routingId;
spot.publish('topic').message('ok').submit();
spot.sendChannel('svc').message('ok').submit();
spot.requestChannel('svc').message('ok').submitAsync();
spot.setSubscription('topic');
spot.subscriptionAt(0);
spot.unsetSubscription('topic');
const spotTopicMessage = new zlink.TopicMessage();
const spotSubscriptionEvent = new zlink.SubscriptionEvent();
spot.subscribe(spotTopicMessage);
spot.receiveSubscriptionEvent(spotSubscriptionEvent);
spot.onSendReady(() => {});
spot.onDispatchEvent((info) => {
  info.actorRef?.generation;
  info.timer?.stop();
  info.recvActorPart(zlink.RecvFlags.DontWait);
});
spot.onRoutedReceive((message) => {
  message.parts;
  message.routingId;
  message.spotRid;
  message.requestSeq;
});
const actorJoin = spot.recvActorJoin(zlink.RecvFlags.DontWait);
if (actorJoin) {
  spot.replyActorJoin(actorJoin, true).message('ok').submit();
}
spot.actorsSnapshot();
spotNode.attachPubIngress(pub);

const monitor = pub.monitorOpen();
monitor.recv();
monitor.onEvent(() => {});
monitor.snapshot();
monitor.close();

const poller = new zlink.Poller();
poller.add(pub, [zlink.PollEventFlag.PollIn], 'pub');
poller.modify(pub, [zlink.PollEventFlag.PollIn, zlink.PollEventFlag.PollOut]);
poller.waitMany(1, 0);
poller.remove(pub);
poller.size;
poller.close();

const thread = new zlink.Thread(() => {});
thread.join();
const counter = new zlink.AtomicCounter(1);
counter.set(counter.inc());
counter.dec();
counter.value();
counter.close();

const diagnosticMessage = zlink.Message.from(Buffer.from('diagnostic'));
const constructedMessage = new zlink.Message(Buffer.from('constructed'));
diagnosticMessage.data();
diagnosticMessage.size();
diagnosticMessage.getProperty('Socket-Type');
diagnosticMessage.refCount();
diagnosticMessage.close();
constructedMessage.data();
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
spot.sendChannel('svc', 'direct-payload');
// @ts-expect-error channel request payloads must go through operation builders.
spot.requestChannel('svc', 'direct-payload');

registry.close();
discovery.close();
spot.close();
spotNode.close();
stream.close();
router.close();
dealer.close();
sub.close();
xpub.close();
pub.close();
ctx.close();
