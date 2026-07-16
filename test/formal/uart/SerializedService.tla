------------------------- MODULE SerializedService -------------------------
EXTENDS Naturals, FiniteSets, Sequences, TLC

(* A bounded, implementation-independent model of SerializedService.
   Each publisher executes exactly one Invoke-like operation.  Event epochs are
   ghost state: a single pending bit may represent several coalesced raises. *)

CONSTANTS Publishers,
          EventBits,
          Contexts,
          WriteEvent,
          CompleteEvent,
          MultiEventPublisher,
          IsrPublisher,
          ThreadContext,
          IsrContext,
          PublisherEvents,
          PublisherContext,
          NoOwner,
          NoContext,
          DurableRelease

DefaultPublisherEvents ==
  [p \in Publishers |->
    IF p = MultiEventPublisher THEN {WriteEvent, CompleteEvent} ELSE {WriteEvent}]

DefaultPublisherContext ==
  [p \in Publishers |-> IF p = IsrPublisher THEN IsrContext ELSE ThreadContext]

ASSUME /\ Publishers # {}
       /\ EventBits # {}
       /\ WriteEvent \in EventBits
       /\ CompleteEvent \in EventBits
       /\ WriteEvent # CompleteEvent
       /\ MultiEventPublisher \in Publishers
       /\ IsrPublisher \in Publishers
       /\ ThreadContext \in Contexts
       /\ IsrContext \in Contexts
       /\ ThreadContext # IsrContext
       /\ PublisherEvents \in [Publishers -> SUBSET EventBits]
       /\ \A p \in Publishers : PublisherEvents[p] # {}
       /\ PublisherContext \in [Publishers -> Contexts]
       /\ NoOwner \notin Publishers
       /\ NoContext \notin Contexts
       /\ DurableRelease \in BOOLEAN

Pcs == {"Publish", "Claim", "Take", "Handle", "Release", "Done"}
OwnerPcs == {"Take", "Handle", "Release"}

ZeroEpoch == [e \in EventBits |-> 0]
EmptySources == [e \in EventBits |-> {}]

HandlerRecord ==
  [owner   : Publishers,
   context : Contexts,
   handled : SUBSET EventBits,
   sources : [EventBits -> SUBSET Contexts]]

VARIABLES pc,
          owner,
          ownerContext,
          events,
          snapshot,
          publishedEpoch,
          snapshotEpoch,
          handledEpoch,
          eventSources,
          snapshotSources,
          handlerLog

vars == <<pc, owner, ownerContext, events, snapshot,
          publishedEpoch, snapshotEpoch, handledEpoch,
          eventSources, snapshotSources, handlerLog>>

Init ==
  /\ pc = [p \in Publishers |-> "Publish"]
  /\ owner = NoOwner
  /\ ownerContext = NoContext
  /\ events = {}
  /\ snapshot = {}
  /\ publishedEpoch = ZeroEpoch
  /\ snapshotEpoch = ZeroEpoch
  /\ handledEpoch = ZeroEpoch
  /\ eventSources = EmptySources
  /\ snapshotSources = EmptySources
  /\ handlerLog = <<>>

Publish(p) ==
  /\ pc[p] = "Publish"
  /\ LET mask == PublisherEvents[p]
         ctx == PublisherContext[p]
     IN /\ events' = events \cup mask
        /\ publishedEpoch' =
             [e \in EventBits |->
                publishedEpoch[e] + (IF e \in mask THEN 1 ELSE 0)]
        /\ eventSources' =
             [e \in EventBits |->
                IF e \in mask
                  THEN eventSources[e] \cup {ctx}
                  ELSE eventSources[e]]
  /\ pc' = [pc EXCEPT ![p] = IF owner = NoOwner THEN "Claim" ELSE "Done"]
  /\ UNCHANGED <<owner, ownerContext, snapshot, snapshotEpoch,
                 handledEpoch, snapshotSources, handlerLog>>

ClaimAndTake(p) ==
  /\ pc[p] = "Claim"
  /\ owner = NoOwner
  /\ events # {}
  /\ owner' = p
  /\ ownerContext' = PublisherContext[p]
  /\ snapshot' = events
  /\ snapshotEpoch' = publishedEpoch
  /\ snapshotSources' = eventSources
  /\ events' = {}
  /\ eventSources' = EmptySources
  /\ pc' = [pc EXCEPT ![p] = "Handle"]
  /\ UNCHANGED <<publishedEpoch, handledEpoch, handlerLog>>

ClaimExit(p) ==
  /\ pc[p] = "Claim"
  /\ \/ owner # NoOwner
     \/ events = {}
  /\ pc' = [pc EXCEPT ![p] = "Done"]
  /\ UNCHANGED <<owner, ownerContext, events, snapshot,
                 publishedEpoch, snapshotEpoch, handledEpoch,
                 eventSources, snapshotSources, handlerLog>>

Take(p) ==
  (* Only a retained owner uses Take after a failed release observed new events. *)
  /\ owner = p
  /\ pc[p] = "Take"
  /\ events # {}
  /\ snapshot' = events
  /\ snapshotEpoch' = publishedEpoch
  /\ snapshotSources' = eventSources
  /\ events' = {}
  /\ eventSources' = EmptySources
  /\ pc' = [pc EXCEPT ![p] = "Handle"]
  /\ UNCHANGED <<owner, ownerContext, publishedEpoch,
                 handledEpoch, handlerLog>>

Handle(p) ==
  /\ owner = p
  /\ pc[p] = "Handle"
  /\ snapshot # {}
  /\ handledEpoch' =
       [e \in EventBits |->
          IF e \in snapshot THEN snapshotEpoch[e] ELSE handledEpoch[e]]
  /\ handlerLog' =
       Append(handlerLog,
              [owner   |-> p,
               context |-> ownerContext,
               handled |-> snapshot,
               sources |-> snapshotSources])
  /\ snapshot' = {}
  /\ snapshotEpoch' = ZeroEpoch
  /\ snapshotSources' = EmptySources
  /\ pc' = [pc EXCEPT ![p] = "Release"]
  /\ UNCHANGED <<owner, ownerContext, events, publishedEpoch, eventSources>>

(* Correct release: an event observed after the handler prevents owner release.
   The same owner takes another snapshot. *)
ReleaseRetry(p) ==
  /\ DurableRelease
  /\ owner = p
  /\ pc[p] = "Release"
  /\ events # {}
  /\ pc' = [pc EXCEPT ![p] = "Take"]
  /\ UNCHANGED <<owner, ownerContext, events, snapshot,
                 publishedEpoch, snapshotEpoch, handledEpoch,
                 eventSources, snapshotSources, handlerLog>>

ReleaseEmpty(p) ==
  /\ owner = p
  /\ pc[p] = "Release"
  /\ events = {}
  /\ owner' = NoOwner
  /\ ownerContext' = NoContext
  /\ pc' = [pc EXCEPT ![p] = "Done"]
  /\ UNCHANGED <<events, snapshot, publishedEpoch, snapshotEpoch,
                 handledEpoch, eventSources, snapshotSources, handlerLog>>

(* Negative model: an unconditional release clears an event published after
   the last snapshot.  That publisher already observed an owner and returned. *)
ReleaseBroken(p) ==
  /\ ~DurableRelease
  /\ owner = p
  /\ pc[p] = "Release"
  /\ events # {}
  /\ owner' = NoOwner
  /\ ownerContext' = NoContext
  /\ events' = {}
  /\ eventSources' = EmptySources
  /\ pc' = [pc EXCEPT ![p] = "Done"]
  /\ UNCHANGED <<snapshot, publishedEpoch, snapshotEpoch,
                 handledEpoch, snapshotSources, handlerLog>>

ActorStep(p) ==
  \/ Publish(p)
  \/ ClaimAndTake(p)
  \/ ClaimExit(p)
  \/ Take(p)
  \/ Handle(p)
  \/ ReleaseRetry(p)
  \/ ReleaseEmpty(p)
  \/ ReleaseBroken(p)

Quiescent ==
  /\ owner = NoOwner
  /\ events = {}
  /\ snapshot = {}
  /\ \A p \in Publishers : pc[p] = "Done"

TerminalStutter == Quiescent /\ UNCHANGED vars

Next ==
  \/ \E p \in Publishers : ActorStep(p)
  \/ TerminalStutter

Fairness == \A p \in Publishers : WF_vars(ActorStep(p))

Spec == Init /\ [][Next]_vars /\ Fairness

TypeOK ==
  /\ pc \in [Publishers -> Pcs]
  /\ owner \in Publishers \cup {NoOwner}
  /\ ownerContext \in Contexts \cup {NoContext}
  /\ events \subseteq EventBits
  /\ snapshot \subseteq EventBits
  /\ publishedEpoch \in [EventBits -> Nat]
  /\ snapshotEpoch \in [EventBits -> Nat]
  /\ handledEpoch \in [EventBits -> Nat]
  /\ eventSources \in [EventBits -> SUBSET Contexts]
  /\ snapshotSources \in [EventBits -> SUBSET Contexts]
  /\ handlerLog \in Seq(HandlerRecord)

ActiveOwners == {p \in Publishers : pc[p] \in OwnerPcs}

OwnerUnique == Cardinality(ActiveOwners) \leq 1

OwnerConsistent ==
  /\ (owner = NoOwner => ActiveOwners = {} /\ ownerContext = NoContext)
  /\ (owner # NoOwner =>
        /\ owner \in Publishers
        /\ ActiveOwners = {owner}
        /\ ownerContext = PublisherContext[owner])

(* Every publication not yet included in a completed handler snapshot must
   still be represented by either the shared event word or the owner snapshot. *)
NoLostEvents ==
  \A e \in EventBits :
    handledEpoch[e] \leq publishedEpoch[e]
    /\ (publishedEpoch[e] > handledEpoch[e]
          => e \in events \/ e \in snapshot)

NoPhantomEvents ==
  /\ \A e \in events : publishedEpoch[e] > handledEpoch[e]
  /\ \A e \in snapshot : snapshotEpoch[e] > handledEpoch[e]

HandlerUsesOwnerContext ==
  \A i \in 1..Len(handlerLog) :
    handlerLog[i].context = PublisherContext[handlerLog[i].owner]

TerminalAccounting ==
  Quiescent => handledEpoch = publishedEpoch

Termination == <>Quiescent

=============================================================================
