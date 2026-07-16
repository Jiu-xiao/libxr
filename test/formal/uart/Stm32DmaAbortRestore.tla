---- MODULE Stm32DmaAbortRestore ----
EXTENDS Naturals, TLC

(***************************************************************************)
(*
 * Focused composition of:
 *
 * 1. EN-authoritative DMA stop acknowledgement.
 * 2. A stale pre-stop TC wrapper.
 * 3. RESTORE_NORMAL consumed before that old wrapper remasks the IRQ domain.
 * 4. Two request|SCHEDULED publishers and one non-reentrant target handler.
 *
 * The constants select the corrected protocol or one intentionally broken
 * variant. Request multiplicity is intentionally coalesced.
 *)
(***************************************************************************)

CONSTANTS
  EnAuthoritative,
  RepublishAfterRemask,
  AtomicPublication

Publishers == {"CONFIG", "WRAPPER"}
RestoreBit == "RESTORE_NORMAL"
ScheduledBit == "SCHEDULED"
SchedulerBits == {RestoreBit, ScheduledBit}

PubStates == {
  "Waiting", "Ready", "Fetch", "ReadScheduled", "PublishRequest",
  "PublishScheduled", "Kick", "Done"
}
HandlerStates == {"Idle", "Exchange", "Process", "Release"}
StopStates == {"Idle", "Armed", "Complete"}
CompletionSources == {"NONE", "OLD_TC", "NEW_TERMINAL"}

VARIABLES
  schedWord,
  pubPC,
  pubSawScheduled,
  kickPending,
  handlerPC,
  snapshot,
  handlerClaims,
  handlerReleases,
  normalMasked,
  restoreCount,
  wrapperRemasked,
  dmaEn,
  stopState,
  oldTcPending,
  newTerminalPending,
  completionSource

vars == <<
  schedWord, pubPC, pubSawScheduled, kickPending, handlerPC, snapshot,
  handlerClaims, handlerReleases, normalMasked, restoreCount,
  wrapperRemasked, dmaEn, stopState, oldTcPending, newTerminalPending,
  completionSource
>>

Init ==
  /\ schedWord = {}
  /\ pubPC = [p \in Publishers |->
       IF p = "CONFIG" THEN "Ready" ELSE "Waiting"]
  /\ pubSawScheduled = [p \in Publishers |-> FALSE]
  /\ kickPending = FALSE
  /\ handlerPC = "Idle"
  /\ snapshot = {}
  /\ handlerClaims = 0
  /\ handlerReleases = 0
  /\ normalMasked = TRUE
  /\ restoreCount = 0
  /\ wrapperRemasked = FALSE
  /\ dmaEn = TRUE
  /\ stopState = "Idle"
  /\ oldTcPending = TRUE
  /\ newTerminalPending = FALSE
  /\ completionSource = "NONE"

(***************************************************************************)
(* The request|SCHEDULED publisher. *)
(***************************************************************************)

PubStart(p) ==
  /\ pubPC[p] = "Ready"
  /\ pubPC' = [pubPC EXCEPT ![p] =
       IF AtomicPublication THEN "Fetch" ELSE "ReadScheduled"]
  /\ UNCHANGED <<schedWord, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, normalMasked,
                  restoreCount, wrapperRemasked, dmaEn, stopState,
                  oldTcPending, newTerminalPending, completionSource>>

PubAtomicFetch(p) ==
  LET wasIdle == ScheduledBit \notin schedWord IN
    /\ AtomicPublication
    /\ pubPC[p] = "Fetch"
    /\ schedWord' = schedWord \cup {RestoreBit, ScheduledBit}
    /\ pubPC' = [pubPC EXCEPT ![p] = IF wasIdle THEN "Kick" ELSE "Done"]
    /\ UNCHANGED <<pubSawScheduled, kickPending, handlerPC, snapshot,
                    handlerClaims, handlerReleases, normalMasked, restoreCount,
                    wrapperRemasked, dmaEn, stopState, oldTcPending,
                    newTerminalPending, completionSource>>

PubReadScheduled(p) ==
  /\ ~AtomicPublication
  /\ pubPC[p] = "ReadScheduled"
  /\ pubSawScheduled' = [pubSawScheduled EXCEPT ![p] =
       ScheduledBit \in schedWord]
  /\ pubPC' = [pubPC EXCEPT ![p] = "PublishRequest"]
  /\ UNCHANGED <<schedWord, kickPending, handlerPC, snapshot, handlerClaims,
                  handlerReleases, normalMasked, restoreCount,
                  wrapperRemasked, dmaEn, stopState, oldTcPending,
                  newTerminalPending, completionSource>>

PubPublishRequest(p) ==
  /\ ~AtomicPublication
  /\ pubPC[p] = "PublishRequest"
  /\ schedWord' = schedWord \cup {RestoreBit}
  /\ pubPC' = [pubPC EXCEPT ![p] = "PublishScheduled"]
  /\ UNCHANGED <<pubSawScheduled, kickPending, handlerPC, snapshot,
                  handlerClaims, handlerReleases, normalMasked, restoreCount,
                  wrapperRemasked, dmaEn, stopState, oldTcPending,
                  newTerminalPending, completionSource>>

PubPublishScheduled(p) ==
  LET shouldKick == ~pubSawScheduled[p] IN
    /\ ~AtomicPublication
    /\ pubPC[p] = "PublishScheduled"
    /\ schedWord' = schedWord \cup {ScheduledBit}
    /\ pubPC' = [pubPC EXCEPT ![p] =
         IF shouldKick THEN "Kick" ELSE "Done"]
    /\ UNCHANGED <<pubSawScheduled, kickPending, handlerPC, snapshot,
                    handlerClaims, handlerReleases, normalMasked, restoreCount,
                    wrapperRemasked, dmaEn, stopState, oldTcPending,
                    newTerminalPending, completionSource>>

PubKick(p) ==
  /\ pubPC[p] = "Kick"
  /\ kickPending' = TRUE
  /\ pubPC' = [pubPC EXCEPT ![p] = "Done"]
  /\ UNCHANGED <<schedWord, pubSawScheduled, handlerPC, snapshot,
                  handlerClaims, handlerReleases, normalMasked, restoreCount,
                  wrapperRemasked, dmaEn, stopState, oldTcPending,
                  newTerminalPending, completionSource>>

(***************************************************************************)
(* Non-reentrant target handler: exchange(SCHEDULED), process, CAS release. *)
(***************************************************************************)

HandlerClaim ==
  /\ handlerPC = "Idle"
  /\ kickPending
  /\ ScheduledBit \in schedWord
  /\ handlerPC' = "Exchange"
  /\ kickPending' = FALSE
  /\ handlerClaims' = handlerClaims + 1
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, snapshot,
                  handlerReleases, normalMasked, restoreCount,
                  wrapperRemasked, dmaEn, stopState, oldTcPending,
                  newTerminalPending, completionSource>>

HandlerExchange ==
  /\ handlerPC = "Exchange"
  /\ snapshot' = schedWord \ {ScheduledBit}
  /\ schedWord' = {ScheduledBit}
  /\ handlerPC' = "Process"
  /\ UNCHANGED <<pubPC, pubSawScheduled, kickPending, handlerClaims,
                  handlerReleases, normalMasked, restoreCount,
                  wrapperRemasked, dmaEn, stopState, oldTcPending,
                  newTerminalPending, completionSource>>

HandlerProcess ==
  /\ handlerPC = "Process"
  /\ normalMasked' =
       IF RestoreBit \in snapshot THEN FALSE ELSE normalMasked
  /\ restoreCount' =
       IF RestoreBit \in snapshot
       THEN IF restoreCount < 2 THEN restoreCount + 1 ELSE restoreCount
       ELSE restoreCount
  /\ snapshot' = {}
  /\ handlerPC' = "Release"
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending,
                  handlerClaims, handlerReleases, wrapperRemasked, dmaEn,
                  stopState, oldTcPending, newTerminalPending,
                  completionSource>>

HandlerReleaseCasSuccess ==
  /\ handlerPC = "Release"
  /\ schedWord = {ScheduledBit}
  /\ schedWord' = {}
  /\ handlerPC' = "Idle"
  /\ handlerReleases' = handlerReleases + 1
  /\ UNCHANGED <<pubPC, pubSawScheduled, kickPending, snapshot,
                  handlerClaims, normalMasked, restoreCount, wrapperRemasked,
                  dmaEn, stopState, oldTcPending, newTerminalPending,
                  completionSource>>

HandlerReleaseCasRetry ==
  /\ handlerPC = "Release"
  /\ schedWord # {ScheduledBit}
  /\ handlerPC' = "Exchange"
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending, snapshot,
                  handlerClaims, handlerReleases, normalMasked, restoreCount,
                  wrapperRemasked, dmaEn, stopState, oldTcPending,
                  newTerminalPending, completionSource>>

(***************************************************************************)
(* The old wrapper runs only after an earlier RESTORE has been processed. *)
(***************************************************************************)

WrapperRemask ==
  /\ ~wrapperRemasked
  /\ restoreCount >= 1
  /\ ~normalMasked
  /\ pubPC["WRAPPER"] = "Waiting"
  /\ normalMasked' = TRUE
  /\ wrapperRemasked' = TRUE
  /\ pubPC' = [pubPC EXCEPT !["WRAPPER"] =
       IF RepublishAfterRemask THEN "Ready" ELSE "Done"]
  /\ UNCHANGED <<schedWord, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, restoreCount,
                  dmaEn, stopState, oldTcPending, newTerminalPending,
                  completionSource>>

(***************************************************************************)
(* DMA stop: the old TC existed before ArmStop and has no generation value. *)
(***************************************************************************)

ArmStop ==
  /\ stopState = "Idle"
  /\ stopState' = "Armed"
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, normalMasked,
                  restoreCount, wrapperRemasked, dmaEn, oldTcPending,
                  newTerminalPending, completionSource>>

HandleOldTcWhileEnabled ==
  /\ EnAuthoritative
  /\ stopState = "Armed"
  /\ dmaEn
  /\ oldTcPending
  /\ oldTcPending' = FALSE
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, normalMasked,
                  restoreCount, wrapperRemasked, dmaEn, stopState,
                  newTerminalPending, completionSource>>

BrokenCompleteOldTcWhileEnabled ==
  /\ ~EnAuthoritative
  /\ stopState = "Armed"
  /\ dmaEn
  /\ oldTcPending
  /\ oldTcPending' = FALSE
  /\ stopState' = "Complete"
  /\ completionSource' = "OLD_TC"
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, normalMasked,
                  restoreCount, wrapperRemasked, dmaEn, newTerminalPending>>

HardwareDisable ==
  /\ stopState = "Armed"
  /\ dmaEn
  /\ dmaEn' = FALSE
  /\ newTerminalPending' = TRUE
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, normalMasked,
                  restoreCount, wrapperRemasked, stopState, oldTcPending,
                  completionSource>>

HandleOldTcAfterDisable ==
  /\ stopState = "Armed"
  /\ ~dmaEn
  /\ oldTcPending
  /\ oldTcPending' = FALSE
  /\ newTerminalPending' = FALSE
  /\ stopState' = "Complete"
  /\ completionSource' = "OLD_TC"
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, normalMasked,
                  restoreCount, wrapperRemasked, dmaEn>>

HandleNewTerminal ==
  /\ stopState = "Armed"
  /\ ~dmaEn
  /\ newTerminalPending
  /\ oldTcPending' = FALSE
  /\ newTerminalPending' = FALSE
  /\ stopState' = "Complete"
  /\ completionSource' = "NEW_TERMINAL"
  /\ UNCHANGED <<schedWord, pubPC, pubSawScheduled, kickPending, handlerPC,
                  snapshot, handlerClaims, handlerReleases, normalMasked,
                  restoreCount, wrapperRemasked, dmaEn>>

Next ==
  \/ \E p \in Publishers : PubStart(p)
  \/ \E p \in Publishers : PubAtomicFetch(p)
  \/ \E p \in Publishers : PubReadScheduled(p)
  \/ \E p \in Publishers : PubPublishRequest(p)
  \/ \E p \in Publishers : PubPublishScheduled(p)
  \/ \E p \in Publishers : PubKick(p)
  \/ HandlerClaim
  \/ HandlerExchange
  \/ HandlerProcess
  \/ HandlerReleaseCasSuccess
  \/ HandlerReleaseCasRetry
  \/ WrapperRemask
  \/ ArmStop
  \/ HandleOldTcWhileEnabled
  \/ BrokenCompleteOldTcWhileEnabled
  \/ HardwareDisable
  \/ HandleOldTcAfterDisable
  \/ HandleNewTerminal

(***************************************************************************)
(* Safety and progress. *)
(***************************************************************************)

TypeOK ==
  /\ EnAuthoritative \in BOOLEAN
  /\ RepublishAfterRemask \in BOOLEAN
  /\ AtomicPublication \in BOOLEAN
  /\ schedWord \subseteq SchedulerBits
  /\ pubPC \in [Publishers -> PubStates]
  /\ pubSawScheduled \in [Publishers -> BOOLEAN]
  /\ kickPending \in BOOLEAN
  /\ handlerPC \in HandlerStates
  /\ snapshot \subseteq {RestoreBit}
  /\ handlerClaims \in Nat
  /\ handlerReleases \in Nat
  /\ handlerReleases <= handlerClaims
  /\ normalMasked \in BOOLEAN
  /\ restoreCount \in 0..2
  /\ wrapperRemasked \in BOOLEAN
  /\ dmaEn \in BOOLEAN
  /\ stopState \in StopStates
  /\ oldTcPending \in BOOLEAN
  /\ newTerminalPending \in BOOLEAN
  /\ completionSource \in CompletionSources

AtMostOneHandler ==
  /\ handlerClaims - handlerReleases \in {0, 1}
  /\ (handlerPC = "Idle") <=> (handlerClaims = handlerReleases)

PublisherCanWake(p) ==
  pubPC[p] \in {
    "Ready", "Fetch", "ReadScheduled", "PublishRequest",
    "PublishScheduled", "Kick"
  }

NoLostWake ==
  ~(
    /\ handlerPC = "Idle"
    /\ RestoreBit \in schedWord
    /\ ~kickPending
    /\ ~(\E p \in Publishers : PublisherCanWake(p))
  )

RestoreCarrier ==
  \/ RestoreBit \in schedWord
  \/ RestoreBit \in snapshot
  \/ \E p \in Publishers : PublisherCanWake(p)

MaskedDomainHasRestoreCarrier ==
  ~normalMasked \/ RestoreCarrier

DmaEnIsAuthoritative ==
  stopState = "Complete" => ~dmaEn

OldTcCompletionRequiresDisabled ==
  completionSource = "OLD_TC" => ~dmaEn

NoOldTerminalCompletionWitness ==
  completionSource # "OLD_TC"

AllPublishersDone ==
  \A p \in Publishers : pubPC[p] = "Done"

SchedulerDrained ==
  /\ schedWord = {}
  /\ snapshot = {}
  /\ handlerPC = "Idle"
  /\ ~kickPending

SystemSettled ==
  /\ stopState = "Complete"
  /\ ~dmaEn
  /\ ~oldTcPending
  /\ ~newTerminalPending
  /\ wrapperRemasked
  /\ ~normalMasked
  /\ restoreCount = 2
  /\ AllPublishersDone
  /\ SchedulerDrained

StopEventuallyCompletes ==
  stopState = "Armed" ~> stopState = "Complete"

RemaskEventuallyRestored ==
  wrapperRemasked ~> ~normalMasked

EventuallySettled ==
  TRUE ~> SystemSettled

FairSpec ==
  /\ \A p \in Publishers : WF_vars(PubStart(p))
  /\ \A p \in Publishers : WF_vars(PubAtomicFetch(p))
  /\ \A p \in Publishers : WF_vars(PubReadScheduled(p))
  /\ \A p \in Publishers : WF_vars(PubPublishRequest(p))
  /\ \A p \in Publishers : WF_vars(PubPublishScheduled(p))
  /\ \A p \in Publishers : WF_vars(PubKick(p))
  /\ WF_vars(HandlerClaim)
  /\ WF_vars(HandlerExchange)
  /\ WF_vars(HandlerProcess)
  /\ WF_vars(HandlerReleaseCasSuccess)
  /\ WF_vars(HandlerReleaseCasRetry)
  /\ WF_vars(WrapperRemask)
  /\ WF_vars(ArmStop)
  /\ WF_vars(HandleOldTcWhileEnabled)
  /\ WF_vars(BrokenCompleteOldTcWhileEnabled)
  /\ WF_vars(HardwareDisable)
  /\ WF_vars(HandleOldTcAfterDisable)
  /\ WF_vars(HandleNewTerminal)

Spec == Init /\ [][Next]_vars /\ FairSpec

=============================================================================
