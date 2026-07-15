package systems.zlink.samples.kotlin.bingo.shared.contracts

typealias AuthenticateReq = Messages.AuthenticateReq
typealias AuthenticateRes = Messages.AuthenticateRes
typealias AuthenticatePlayerReq = Messages.AuthenticatePlayerReq
typealias AuthenticatePlayerRes = Messages.AuthenticatePlayerRes
typealias EnsurePlayerActorReq = Messages.EnsurePlayerActorReq
typealias ActorRefWire = Messages.ActorRefWire
typealias EnsurePlayerActorRes = Messages.EnsurePlayerActorRes
typealias MatchBingoReq = Messages.MatchBingoReq
typealias MatchBingoRes = Messages.MatchBingoRes
typealias MatchBingoApiReq = Messages.MatchBingoApiReq
typealias MatchBingoApiRes = Messages.MatchBingoApiRes
typealias AllocateBingoRoomReq = Messages.AllocateBingoRoomReq
typealias AllocateBingoRoomRes = Messages.AllocateBingoRoomRes
typealias BingoRoomSettingsPayload = Messages.BingoRoomSettingsPayload
typealias BingoRoomJoinReq = Messages.BingoRoomJoinReq
typealias BingoRoomJoinRes = Messages.BingoRoomJoinRes
typealias SubmitBingoCardReq = Messages.SubmitBingoCardReq
typealias SubmitBingoCardRes = Messages.SubmitBingoCardRes
typealias ObserveBingoEventsReq = Messages.ObserveBingoEventsReq
typealias ObserveBingoEventsRes = Messages.ObserveBingoEventsRes
typealias StopObservingBingoEventsReq = Messages.StopObservingBingoEventsReq
typealias StopObservingBingoEventsRes = Messages.StopObservingBingoEventsRes
typealias PlayerJoinedNotify = Messages.PlayerJoinedNotify
typealias BingoGameStartedNotify = Messages.BingoGameStartedNotify
typealias BingoNumberDrawnNotify = Messages.BingoNumberDrawnNotify
typealias BingoStateNotify = Messages.BingoStateNotify
typealias BingoGameEndedNotify = Messages.BingoGameEndedNotify
typealias BingoRewardAnnouncedNotify = Messages.BingoRewardAnnouncedNotify
typealias BingoRewardAcquiredEvent = Messages.BingoRewardAcquiredEvent
typealias BingoRoomState = Messages.BingoRoomState
typealias BingoPlayerState = Messages.BingoPlayerState

val SubmitBingoCardReq.card: List<Int>
    get() = cardList

val BingoRoomState.drawnNumbers: List<Int>
    get() = drawnNumbersList

val BingoRoomState.players: List<BingoPlayerState>
    get() = playersList

val BingoRoomState.winners: List<String>
    get() = winnersList

val BingoPlayerState.card: List<Int>
    get() = cardList

val BingoPlayerState.marks: List<Boolean>
    get() = marksList

fun AuthenticateReq(accessToken: String): AuthenticateReq =
    Messages.AuthenticateReq.newBuilder()
        .setAccessToken(accessToken)
        .build()

fun AuthenticateRes(
    actorId: String,
    displayName: String,
    actorNodeRid: String,
): AuthenticateRes =
    Messages.AuthenticateRes.newBuilder()
        .setActorId(actorId)
        .setDisplayName(displayName)
        .setActorNodeRid(actorNodeRid)
        .build()

fun AuthenticatePlayerReq(accessToken: String): AuthenticatePlayerReq =
    Messages.AuthenticatePlayerReq.newBuilder()
        .setAccessToken(accessToken)
        .build()

fun AuthenticatePlayerRes(
    accepted: Boolean,
    actorId: String,
    displayName: String,
    reason: String?,
): AuthenticatePlayerRes =
    Messages.AuthenticatePlayerRes.newBuilder()
        .setAccepted(accepted)
        .setActorId(actorId)
        .setDisplayName(displayName)
        .apply {
            if (reason != null) {
                setReason(reason)
            }
        }
        .build()

fun EnsurePlayerActorReq(
    actorId: String,
    displayName: String,
    preferredActorNodeRid: String,
): EnsurePlayerActorReq =
    Messages.EnsurePlayerActorReq.newBuilder()
        .setActorId(actorId)
        .setDisplayName(displayName)
        .setPreferredActorNodeRid(preferredActorNodeRid)
        .build()

fun ActorRefWire(
    nodeRid: String,
    actorId: String,
    generation: Long,
): ActorRefWire =
    Messages.ActorRefWire.newBuilder()
        .setNodeRid(nodeRid)
        .setActorId(actorId)
        .setGeneration(generation)
        .build()

fun EnsurePlayerActorRes(
    actorId: String,
    actorType: String,
    actor: ActorRefWire,
): EnsurePlayerActorRes =
    Messages.EnsurePlayerActorRes.newBuilder()
        .setActorId(actorId)
        .setActorType(actorType)
        .setActor(actor)
        .build()

fun MatchBingoReq(mode: String): MatchBingoReq =
    Messages.MatchBingoReq.newBuilder()
        .setMode(mode)
        .build()

fun MatchBingoRes(
    roomId: String,
    state: BingoRoomState,
    roomOwnerNodeRid: String,
): MatchBingoRes =
    Messages.MatchBingoRes.newBuilder()
        .setRoomId(roomId)
        .setState(state)
        .setRoomOwnerNodeRid(roomOwnerNodeRid)
        .build()

fun MatchBingoApiReq(
    actorId: String,
    displayName: String,
    mode: String,
    actorNodeRid: String,
): MatchBingoApiReq =
    Messages.MatchBingoApiReq.newBuilder()
        .setActorId(actorId)
        .setDisplayName(displayName)
        .setMode(mode)
        .setActorNodeRid(actorNodeRid)
        .build()

fun MatchBingoApiRes(
    roomId: String,
    roomOwnerNodeRid: String,
): MatchBingoApiRes =
    Messages.MatchBingoApiRes.newBuilder()
        .setRoomId(roomId)
        .setRoomOwnerNodeRid(roomOwnerNodeRid)
        .build()

fun AllocateBingoRoomReq(
    actorId: String,
    mode: String,
    preferredOwnerNodeRid: String,
): AllocateBingoRoomReq =
    Messages.AllocateBingoRoomReq.newBuilder()
        .setActorId(actorId)
        .setMode(mode)
        .setPreferredOwnerNodeRid(preferredOwnerNodeRid)
        .build()

fun AllocateBingoRoomRes(
    roomId: String,
    roomOwnerNodeRid: String,
): AllocateBingoRoomRes =
    Messages.AllocateBingoRoomRes.newBuilder()
        .setRoomId(roomId)
        .setRoomOwnerNodeRid(roomOwnerNodeRid)
        .build()

fun BingoRoomSettingsPayload(
    mode: String,
    roomName: String,
    requiredPlayers: Int,
    maxDrawNumber: Int,
    drawPeriodMillis: Long,
    observedRoomId: String,
): BingoRoomSettingsPayload =
    Messages.BingoRoomSettingsPayload.newBuilder()
        .setMode(mode)
        .setRoomName(roomName)
        .setRequiredPlayers(requiredPlayers)
        .setMaxDrawNumber(maxDrawNumber)
        .setDrawPeriodMillis(drawPeriodMillis)
        .setObservedRoomId(observedRoomId)
        .build()

fun BingoRoomJoinReq(
    roomId: String,
    actorId: String,
    displayName: String,
    observeOnly: Boolean,
): BingoRoomJoinReq =
    Messages.BingoRoomJoinReq.newBuilder()
        .setRoomId(roomId)
        .setActorId(actorId)
        .setDisplayName(displayName)
        .setObserveOnly(observeOnly)
        .build()

fun BingoRoomJoinRes(state: BingoRoomState): BingoRoomJoinRes =
    Messages.BingoRoomJoinRes.newBuilder()
        .setState(state)
        .build()

fun SubmitBingoCardReq(
    roomId: String,
    card: List<Int>,
): SubmitBingoCardReq =
    Messages.SubmitBingoCardReq.newBuilder()
        .setRoomId(roomId)
        .addAllCard(card)
        .build()

fun SubmitBingoCardRes(state: BingoRoomState): SubmitBingoCardRes =
    Messages.SubmitBingoCardRes.newBuilder()
        .setState(state)
        .build()

fun ObserveBingoEventsReq(roomId: String): ObserveBingoEventsReq =
    Messages.ObserveBingoEventsReq.newBuilder()
        .setRoomId(roomId)
        .build()

fun ObserveBingoEventsRes(
    subscribed: Boolean,
    observerNodeRid: String,
): ObserveBingoEventsRes =
    Messages.ObserveBingoEventsRes.newBuilder()
        .setSubscribed(subscribed)
        .setObserverNodeRid(observerNodeRid)
        .build()

fun StopObservingBingoEventsReq(roomId: String): StopObservingBingoEventsReq =
    Messages.StopObservingBingoEventsReq.newBuilder()
        .setRoomId(roomId)
        .build()

fun StopObservingBingoEventsRes(
    stopped: Boolean,
    observerNodeRid: String,
): StopObservingBingoEventsRes =
    Messages.StopObservingBingoEventsRes.newBuilder()
        .setStopped(stopped)
        .setObserverNodeRid(observerNodeRid)
        .build()

fun PlayerJoinedNotify(
    roomId: String,
    actorId: String,
    displayName: String,
    seat: Int,
    isHost: Boolean,
    state: BingoRoomState,
): PlayerJoinedNotify =
    Messages.PlayerJoinedNotify.newBuilder()
        .setRoomId(roomId)
        .setActorId(actorId)
        .setDisplayName(displayName)
        .setSeat(seat)
        .setIsHost(isHost)
        .setState(state)
        .build()

fun BingoGameStartedNotify(state: BingoRoomState): BingoGameStartedNotify =
    Messages.BingoGameStartedNotify.newBuilder()
        .setState(state)
        .build()

fun BingoNumberDrawnNotify(
    roomId: String,
    drawSeq: Int,
    number: Int,
    state: BingoRoomState,
): BingoNumberDrawnNotify =
    Messages.BingoNumberDrawnNotify.newBuilder()
        .setRoomId(roomId)
        .setDrawSeq(drawSeq)
        .setNumber(number)
        .setState(state)
        .build()

fun BingoStateNotify(state: BingoRoomState): BingoStateNotify =
    Messages.BingoStateNotify.newBuilder()
        .setState(state)
        .build()

fun BingoGameEndedNotify(state: BingoRoomState): BingoGameEndedNotify =
    Messages.BingoGameEndedNotify.newBuilder()
        .setState(state)
        .build()

fun BingoRewardAnnouncedNotify(
    roomId: String,
    actorId: String,
    drawSeq: Int,
    itemId: String,
    itemName: String,
    rarity: String,
    receivingSpotNodeRid: String,
): BingoRewardAnnouncedNotify =
    Messages.BingoRewardAnnouncedNotify.newBuilder()
        .setRoomId(roomId)
        .setActorId(actorId)
        .setDrawSeq(drawSeq)
        .setItemId(itemId)
        .setItemName(itemName)
        .setRarity(rarity)
        .setReceivingSpotNodeRid(receivingSpotNodeRid)
        .build()

fun BingoRewardAcquiredEvent(
    roomId: String,
    actorId: String,
    drawSeq: Int,
    itemId: String,
    itemName: String,
    rarity: String,
): BingoRewardAcquiredEvent =
    Messages.BingoRewardAcquiredEvent.newBuilder()
        .setRoomId(roomId)
        .setActorId(actorId)
        .setDrawSeq(drawSeq)
        .setItemId(itemId)
        .setItemName(itemName)
        .setRarity(rarity)
        .build()

fun BingoRoomState(
    roomId: String,
    status: String,
    hostActorId: String,
    canStart: Boolean,
    drawSeq: Int,
    lastDrawnNumber: Int?,
    drawnNumbers: List<Int>,
    players: List<BingoPlayerState>,
    winners: List<String>,
): BingoRoomState =
    Messages.BingoRoomState.newBuilder()
        .setRoomId(roomId)
        .setStatus(status)
        .setHostActorId(hostActorId)
        .setCanStart(canStart)
        .setDrawSeq(drawSeq)
        .apply {
            if (lastDrawnNumber != null) {
                setLastDrawnNumber(lastDrawnNumber)
            }
        }
        .addAllDrawnNumbers(drawnNumbers)
        .addAllPlayers(players)
        .addAllWinners(winners)
        .build()

fun BingoPlayerState(
    actorId: String,
    displayName: String,
    seat: Int,
    isHost: Boolean,
    card: List<Int>,
    marks: List<Boolean>,
    completedLines: Int,
): BingoPlayerState =
    Messages.BingoPlayerState.newBuilder()
        .setActorId(actorId)
        .setDisplayName(displayName)
        .setSeat(seat)
        .setIsHost(isHost)
        .addAllCard(card)
        .addAllMarks(marks)
        .setCompletedLines(completedLines)
        .build()
