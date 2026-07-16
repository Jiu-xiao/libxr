---- MODULE Stm32IrqScheduler ----
EXTENDS Naturals, TLC

(***************************************************************************)
(*
 * STM32 IRQ-domain scheduler candidate.
 *
 * The production candidate stores TX/CONFIG request bits and SCHEDULED in
 * one atomic uint32_t.  A publisher performs one fetch_or(request |
 * SCHEDULED).  Only the publisher that observes an idle -> scheduled
 * transition delivers a kick.  A kick is level-triggered and represented by
 * a boolean here, so duplicate kicks coalesce and a delayed kick can be
 * dropped after the work has already been drained.
 *
 * AtomicPublication = FALSE is an intentionally broken publication where a
 * stale read of SCHEDULED is separated from the request and schedule writes.
 * ReleaseUsesCAS = FALSE is an intentionally broken release that clears the
 * whole word instead of CAS(SCHEDULED, 0) followed by a retry.
 *)
(***************************************************************************)

CONSTANTS AtomicPublication, ReleaseUsesCAS

Publishers == {"P1", "P2"}
RequestBits == {"DEFERRED_SCAN", "RESTORE_NORMAL"}
ScheduledBit == "SCHEDULED"
AllBits == RequestBits \cup {ScheduledBit}

PubStates == {
  "Ready", "Fetch", "ReadScheduled", "PublishRequest", "PublishScheduled",
  "Kick", "Done"
}
HandlerStates == {"Idle", "Exchange", "Process", "Release"}

ReqFor(p) == IF p = "P1" THEN "DEFERRED_SCAN" ELSE "RESTORE_NORMAL"

VARIABLES
  word,
  pubPC,
  pubSawScheduled,
  published,
  snapshot,
  processed,
  handlerPC,
  kickPending,
  handlerClaims,
  handlerReleases,
  lateKickRemaining

vars == <<
  word, pubPC, pubSawScheduled, published, snapshot, processed,
  handlerPC, kickPending, handlerClaims, handlerReleases, lateKickRemaining
>>

Init ==
  /\ word = {}
  /\ pubPC = [p \in Publishers |-> "Ready"]
  /\ pubSawScheduled = [p \in Publishers |-> FALSE]
  /\ published = {}
  /\ snapshot = {}
  /\ processed = {}
  /\ handlerPC = "Idle"
  /\ kickPending = FALSE
  /\ handlerClaims = 0
  /\ handlerReleases = 0
  /\ lateKickRemaining = 1

PubStart(p) ==
  /\ pubPC[p] = "Ready"
  /\ pubPC' = [pubPC EXCEPT ![p] =
       IF AtomicPublication THEN "Fetch" ELSE "ReadScheduled"]
  /\ UNCHANGED <<word, pubSawScheduled, published, snapshot, processed,
                  handlerPC, kickPending, handlerClaims, handlerReleases,
                  lateKickRemaining>>

PubAtomicFetch(p) ==
  LET wasIdle == ScheduledBit \notin word IN
    /\ AtomicPublication
    /\ pubPC[p] = "Fetch"
    /\ word' = word \cup {ReqFor(p), ScheduledBit}
    /\ published' = published \cup {ReqFor(p)}
    /\ pubPC' = [pubPC EXCEPT ![p] = IF wasIdle THEN "Kick" ELSE "Done"]
    /\ UNCHANGED <<pubSawScheduled, snapshot, processed, handlerPC,
                    kickPending, handlerClaims, handlerReleases,
                    lateKickRemaining>>

PubReadScheduled(p) ==
  /\ ~AtomicPublication
  /\ pubPC[p] = "ReadScheduled"
  /\ pubSawScheduled' = [pubSawScheduled EXCEPT ![p] =
       ScheduledBit \in word]
  /\ pubPC' = [pubPC EXCEPT ![p] = "PublishRequest"]
  /\ UNCHANGED <<word, published, snapshot, processed, handlerPC,
                  kickPending, handlerClaims, handlerReleases,
                  lateKickRemaining>>

PubPublishRequest(p) ==
  /\ ~AtomicPublication
  /\ pubPC[p] = "PublishRequest"
  /\ word' = word \cup {ReqFor(p)}
  /\ published' = published \cup {ReqFor(p)}
  /\ pubPC' = [pubPC EXCEPT ![p] = "PublishScheduled"]
  /\ UNCHANGED <<pubSawScheduled, snapshot, processed, handlerPC,
                  kickPending, handlerClaims, handlerReleases,
                  lateKickRemaining>>

PubPublishScheduled(p) ==
  LET shouldKick == ~pubSawScheduled[p] IN
    /\ ~AtomicPublication
    /\ pubPC[p] = "PublishScheduled"
    /\ word' = word \cup {ScheduledBit}
    /\ pubPC' = [pubPC EXCEPT ![p] = IF shouldKick THEN "Kick" ELSE "Done"]
    /\ UNCHANGED <<pubSawScheduled, published, snapshot, processed,
                    handlerPC, kickPending, handlerClaims, handlerReleases,
                    lateKickRemaining>>

PubKick(p) ==
  /\ pubPC[p] = "Kick"
  /\ kickPending' = TRUE
  /\ pubPC' = [pubPC EXCEPT ![p] = "Done"]
  /\ UNCHANGED <<word, pubSawScheduled, published, snapshot, processed,
                  handlerPC, handlerClaims, handlerReleases,
                  lateKickRemaining>>

DeliverLateKick ==
  /\ lateKickRemaining = 1
  /\ lateKickRemaining' = 0
  /\ kickPending' = TRUE
  /\ UNCHANGED <<word, pubPC, pubSawScheduled, published, snapshot,
                  processed, handlerPC, handlerClaims, handlerReleases>>

(***************************************************************************)
(* Service ownership and the non-reentrant handler. *)
(***************************************************************************)

HandlerClaim ==
  /\ handlerPC = "Idle"
  /\ kickPending
  /\ ScheduledBit \in word
  /\ handlerPC' = "Exchange"
  /\ kickPending' = FALSE
  /\ handlerClaims' = handlerClaims + 1
  /\ UNCHANGED <<word, pubPC, pubSawScheduled, published, snapshot,
                  processed, handlerReleases, lateKickRemaining>>

DropLateKick ==
  /\ handlerPC = "Idle"
  /\ kickPending
  /\ ScheduledBit \notin word
  /\ kickPending' = FALSE
  /\ UNCHANGED <<word, pubPC, pubSawScheduled, published, snapshot,
                  processed, handlerPC, handlerClaims, handlerReleases,
                  lateKickRemaining>>

HandlerExchange ==
  /\ handlerPC = "Exchange"
  /\ snapshot' = word \ {ScheduledBit}
  /\ word' = {ScheduledBit}
  /\ handlerPC' = "Process"
  /\ UNCHANGED <<pubPC, pubSawScheduled, published, processed,
                  kickPending, handlerClaims, handlerReleases,
                  lateKickRemaining>>

HandlerProcess ==
  /\ handlerPC = "Process"
  /\ processed' = processed \cup snapshot
  /\ snapshot' = {}
  /\ handlerPC' = "Release"
  /\ UNCHANGED <<word, pubPC, pubSawScheduled, published,
                  kickPending, handlerClaims, handlerReleases,
                  lateKickRemaining>>

HandlerReleaseCasSuccess ==
  /\ ReleaseUsesCAS
  /\ handlerPC = "Release"
  /\ word = {ScheduledBit}
  /\ word' = {}
  /\ handlerPC' = "Idle"
  /\ handlerReleases' = handlerReleases + 1
  /\ UNCHANGED <<pubPC, pubSawScheduled, published, snapshot, processed,
                  kickPending, handlerClaims, lateKickRemaining>>

HandlerReleaseCasRetry ==
  /\ ReleaseUsesCAS
  /\ handlerPC = "Release"
  /\ word # {ScheduledBit}
  /\ handlerPC' = "Exchange"
  /\ UNCHANGED <<word, pubPC, pubSawScheduled, published, snapshot,
                  processed, kickPending, handlerClaims, handlerReleases,
                  lateKickRemaining>>

HandlerReleaseBroken ==
  /\ ~ReleaseUsesCAS
  /\ handlerPC = "Release"
  /\ word' = {}
  /\ handlerPC' = "Idle"
  /\ handlerReleases' = handlerReleases + 1
  /\ UNCHANGED <<pubPC, pubSawScheduled, published, snapshot, processed,
                  kickPending, handlerClaims, lateKickRemaining>>

Next ==
  \/ \E p \in Publishers : PubStart(p)
  \/ \E p \in Publishers : PubAtomicFetch(p)
  \/ \E p \in Publishers : PubReadScheduled(p)
  \/ \E p \in Publishers : PubPublishRequest(p)
  \/ \E p \in Publishers : PubPublishScheduled(p)
  \/ \E p \in Publishers : PubKick(p)
  \/ DeliverLateKick
  \/ HandlerClaim
  \/ DropLateKick
  \/ HandlerExchange
  \/ HandlerProcess
  \/ HandlerReleaseCasSuccess
  \/ HandlerReleaseCasRetry
  \/ HandlerReleaseBroken

(***************************************************************************)
(* Type and safety properties. *)
(***************************************************************************)

TypeOK ==
  /\ AtomicPublication \in BOOLEAN
  /\ ReleaseUsesCAS \in BOOLEAN
  /\ word \subseteq AllBits
  /\ pubPC \in [Publishers -> PubStates]
  /\ pubSawScheduled \in [Publishers -> BOOLEAN]
  /\ published \subseteq RequestBits
  /\ snapshot \subseteq RequestBits
  /\ processed \subseteq RequestBits
  /\ handlerPC \in HandlerStates
  /\ kickPending \in BOOLEAN
  /\ handlerClaims \in Nat
  /\ handlerReleases \in Nat
  /\ handlerReleases <= handlerClaims
  /\ lateKickRemaining \in 0..1

NoRequestProcessedBeforePublished ==
  processed \subseteq published

AtMostOneHandler ==
  /\ handlerClaims - handlerReleases \in {0, 1}
  /\ (handlerPC = "Idle") <=> (handlerClaims = handlerReleases)

(***************************************************************************)
(* A nonempty word with no owner must have a kick or an in-flight publisher.
 * In the corrected model this is the linearized no-lost-wake condition. *)
(***************************************************************************)

NoLostWake ==
  ~(
    /\ handlerPC = "Idle"
    /\ (word \ {ScheduledBit}) # {}
    /\ ~kickPending
    /\ \A p \in Publishers : pubPC[p] = "Done"
  )

PendingRequest(r) ==
  \/ r \in (word \cup snapshot)
  \/ \E p \in Publishers :
       ReqFor(p) = r /\ pubPC[p] # "Done"

NoPublishedRequestLoss ==
  \A r \in published \ processed : PendingRequest(r)

AllPublishersDone ==
  \A p \in Publishers : pubPC[p] = "Done"

NoOutstandingWork ==
  /\ published = processed
  /\ word = {}
  /\ snapshot = {}
  /\ handlerPC = "Idle"
  /\ ~kickPending
  /\ lateKickRemaining = 0

EventuallyDrained == AllPublishersDone ~> NoOutstandingWork

(***************************************************************************)
(* Fairness is intentionally weak and applies to every bounded publisher
 * and every service continuation.  No fairness is assumed for an external
 * unbounded event source: each publisher has exactly one publication. *)
(***************************************************************************)

FairSpec ==
  /\ \A p \in Publishers : WF_vars(PubStart(p))
  /\ \A p \in Publishers : WF_vars(PubAtomicFetch(p))
  /\ \A p \in Publishers : WF_vars(PubReadScheduled(p))
  /\ \A p \in Publishers : WF_vars(PubPublishRequest(p))
  /\ \A p \in Publishers : WF_vars(PubPublishScheduled(p))
  /\ \A p \in Publishers : WF_vars(PubKick(p))
  /\ WF_vars(DeliverLateKick)
  /\ WF_vars(HandlerClaim)
  /\ WF_vars(DropLateKick)
  /\ WF_vars(HandlerExchange)
  /\ WF_vars(HandlerProcess)
  /\ WF_vars(HandlerReleaseCasSuccess)
  /\ WF_vars(HandlerReleaseCasRetry)
  /\ WF_vars(HandlerReleaseBroken)

Spec == Init /\ [][Next]_vars /\ FairSpec

=============================================================================
