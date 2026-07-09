/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotCompletion;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotHandler;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotResult;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorJoinHandler;
import systems.zlink.contracts.service.spot.ActorJoinResult;
import systems.zlink.contracts.service.spot.ActorLookupHandler;
import systems.zlink.contracts.service.spot.ActorLookupResult;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.sockets.RequestResult;

final class ActorRequestFutureAdapters {
    private ActorRequestFutureAdapters() {
    }

    static CompletableFuture<List<Message>> reply(
        Consumer<ReplyHandler> submitter) {
        CompletableFuture<List<Message>> future = new CompletableFuture<>();
        submitter.accept((result, parts) ->
            completeMessages(future, result, parts));
        return future;
    }

    static CompletableFuture<List<Message>> request(
        Consumer<RequestCallback> submitter) {
        CompletableFuture<List<Message>> future = new CompletableFuture<>();
        submitter.accept((result, replyParts) ->
            completeMessages(future, result, replyParts));
        return future;
    }

    static CompletableFuture<ActorLookupResult> lookup(
        Consumer<ActorLookupHandler> submitter) {
        CompletableFuture<ActorLookupResult> future = new CompletableFuture<>();
        submitter.accept(result -> {
            if (result.result() == RequestResult.OK) {
                future.complete(result);
            } else {
                future.completeExceptionally(
                    new ZlinkRequestException(result.result()));
            }
        });
        return future;
    }

    static CompletableFuture<ActorJoinCompletion> actorJoin(
        Consumer<ActorJoinHandler> submitter) {
        CompletableFuture<ActorJoinCompletion> future =
            new CompletableFuture<>();
        submitter.accept((result, replyParts) ->
            completeActorJoin(future, result, replyParts));
        return future;
    }

    static CompletableFuture<ActorJoinEntrySpotCompletion> joinEntrySpot(
        Consumer<ActorJoinEntrySpotHandler> submitter) {
        CompletableFuture<ActorJoinEntrySpotCompletion> future =
            new CompletableFuture<>();
        submitter.accept((result, replyParts) ->
            completeJoinEntrySpot(future, result, replyParts));
        return future;
    }

    private static void completeMessages(CompletableFuture<List<Message>> future,
                                         RequestResult result,
                                         List<Message> parts) {
        if (result == RequestResult.OK) {
            future.complete(parts);
        } else {
            future.completeExceptionally(new ZlinkRequestException(result));
        }
    }

    private static void completeActorJoin(
        CompletableFuture<ActorJoinCompletion> future,
        ActorJoinResult result,
        List<Message> replyParts) {
        if (result.result() == RequestResult.OK) {
            future.complete(new ActorJoinCompletion(result, replyParts));
        } else {
            future.completeExceptionally(
                new ZlinkRequestException(result.result()));
        }
    }

    private static void completeJoinEntrySpot(
        CompletableFuture<ActorJoinEntrySpotCompletion> future,
        ActorJoinEntrySpotResult result,
        List<Message> replyParts) {
        if (result.result() == RequestResult.OK) {
            future.complete(
                new ActorJoinEntrySpotCompletion(result, replyParts));
        } else {
            future.completeExceptionally(
                new ZlinkRequestException(result.result()));
        }
    }
}
