// SPDX-License-Identifier: MPL-2.0

package native

// --- common state ---

type actorJoinCallback func(result ActorJoinResult, parts []*Message)
type actorJoinEntrySpotCallback func(result ActorJoinEntrySpotResult)
type actorLookupCallback func(result ActorLookupResult)
type requestPartsCallback func(result RequestResult, parts []*Message)

type actorJoinCallbackState struct {
	result chan actorJoinTrampolineResult
	done   chan struct{}
	once   completionGuard
}

type actorJoinTrampolineResult struct {
	result ActorJoinResult
	parts  []*Message
}

type actorJoinEntrySpotCallbackState struct {
	result chan ActorJoinEntrySpotResult
	done   chan struct{}
	once   completionGuard
}

type actorLookupCallbackState struct {
	result chan ActorLookupResult
	done   chan struct{}
	once   completionGuard
}

type completionGuard struct {
	closed bool
}

func (g *completionGuard) close(done chan struct{}) {
	if g.closed {
		return
	}
	g.closed = true
	close(done)
}
