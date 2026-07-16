------------------------- MODULE UartConfigAbortJoin -------------------------
EXTENDS FiniteSets, Naturals, TLC

(***************************************************************************)
(* Focused CONFIG DMA-stop join model.                                     *)
(*                                                                         *)
(* Scope: one CONFIG request, TX and RX directions, stopped-before-launch  *)
(* completion, asynchronous stop arming, an IRQ that may first observe     *)
(* EN=1 and retry, EN-authoritative completion, apply from inside the final *)
(* stop IRQ after its hardware readback, one fixed TX prefix, and one WRITE *)
(* accepted while CONFIG owns the hardware.                                 *)
(*                                                                         *)
(* Excluded: register encodings, DMA data movement, WritePort producer      *)
(* admission, user callback bodies, cache maintenance, and IRQ routing.     *)
(***************************************************************************)

CONSTANTS BreakEarlyJoin,
          BreakNoWriteRescan,
          BreakStopWithoutProof,
          BreakRetainOldTxRetry

ASSUME /\ BreakEarlyJoin \in BOOLEAN
       /\ BreakNoWriteRescan \in BOOLEAN
       /\ BreakStopWithoutProof \in BOOLEAN
       /\ BreakRetainOldTxRetry \in BOOLEAN

Dirs == {"TX", "RX"}
Phases == {"Idle", "Launching", "Stopping", "Quiescent", "Applied", "Released"}

VARIABLES
  phase,
  dmaEnabled,
  stopRequested,
  launchReturned,
  armResolved,
  abortPending,
  asyncStopArmed,
  stopIrqRunning,
  stopIrqChecked,
  configBoundary,
  rxRunning,
  writeQueued,
  newTxStarted,
  applyCount,
  releaseCount,
  earlyApply,
  unsafeStopCompletion,
  appliedInsideStopIrq,
  oldTxRetryPending

vars == <<phase, dmaEnabled, stopRequested, launchReturned, armResolved,
          abortPending, asyncStopArmed, stopIrqRunning, stopIrqChecked,
          configBoundary, rxRunning, writeQueued, newTxStarted, applyCount,
          releaseCount, earlyApply, unsafeStopCompletion,
          appliedInsideStopIrq, oldTxRetryPending>>

Init ==
  /\ phase = "Idle"
  /\ dmaEnabled \in SUBSET Dirs
  /\ stopRequested = {}
  /\ launchReturned = {}
  /\ armResolved = {}
  /\ abortPending = {}
  /\ asyncStopArmed = {}
  /\ stopIrqRunning = {}
  /\ stopIrqChecked = {}
  /\ configBoundary = FALSE
  /\ rxRunning = TRUE
  /\ writeQueued = FALSE
  /\ newTxStarted = FALSE
  /\ applyCount = 0
  /\ releaseCount = 0
  /\ earlyApply = FALSE
  /\ unsafeStopCompletion = FALSE
  /\ appliedInsideStopIrq = FALSE
  /\ oldTxRetryPending = TRUE

(***************************************************************************)
(* This action abstracts fixed-boundary capture plus successful CONFIG      *)
(* admission. The claim CAS retires a TX retry for the old prefix.          *)
(***************************************************************************)
RequestConfig ==
  /\ phase = "Idle"
  /\ phase' = "Launching"
  /\ stopRequested' = {}
  /\ launchReturned' = {}
  /\ armResolved' = {}
  /\ abortPending' = Dirs
  /\ asyncStopArmed' = {}
  /\ stopIrqRunning' = {}
  /\ stopIrqChecked' = {}
  /\ configBoundary' = TRUE
  /\ rxRunning' = FALSE
  /\ oldTxRetryPending' = IF BreakRetainOldTxRetry THEN TRUE ELSE FALSE
  /\ UNCHANGED <<dmaEnabled, writeQueued, newTxStarted, applyCount,
                 releaseCount, earlyApply, unsafeStopCompletion,
                 appliedInsideStopIrq>>

(***************************************************************************)
(* Each backend stop call returns before any asynchronous direction is      *)
(* armed. A direction already at EN=0 completes synchronously.              *)
(***************************************************************************)

LaunchStop(d) ==
  /\ phase = "Launching"
  /\ d \notin launchReturned
  /\ launchReturned' = launchReturned \cup {d}
  /\ IF d \in dmaEnabled
        THEN /\ stopRequested' = stopRequested \cup {d}
             /\ UNCHANGED <<armResolved, abortPending>>
        ELSE /\ stopRequested' = stopRequested
             /\ armResolved' = armResolved \cup {d}
             /\ abortPending' = abortPending \ {d}
  /\ UNCHANGED <<phase, dmaEnabled, asyncStopArmed, stopIrqRunning,
                 stopIrqChecked, configBoundary, rxRunning, writeQueued,
                 newTxStarted, applyCount, releaseCount, earlyApply,
                 unsafeStopCompletion, appliedInsideStopIrq,
                 oldTxRetryPending>>

HardwareStop(d) ==
  /\ d \in stopRequested
  /\ d \in dmaEnabled
  /\ dmaEnabled' = dmaEnabled \ {d}
  /\ UNCHANGED <<phase, stopRequested, launchReturned, armResolved,
                 abortPending, asyncStopArmed, stopIrqRunning,
                 stopIrqChecked, configBoundary, rxRunning, writeQueued,
                 newTxStarted, applyCount, releaseCount, earlyApply,
                 unsafeStopCompletion, appliedInsideStopIrq,
                 oldTxRetryPending>>

ResolveAsyncStop(d) ==
  /\ phase = "Launching"
  /\ launchReturned = Dirs
  /\ d \in launchReturned
  /\ d \notin armResolved
  /\ armResolved' = armResolved \cup {d}
  /\ IF d \in dmaEnabled
        THEN /\ asyncStopArmed' = asyncStopArmed \cup {d}
             /\ abortPending' = abortPending
        ELSE /\ asyncStopArmed' = asyncStopArmed
             /\ abortPending' = abortPending \ {d}
  /\ UNCHANGED <<phase, dmaEnabled, stopRequested, launchReturned,
                 stopIrqRunning, stopIrqChecked, configBoundary, rxRunning,
                 writeQueued, newTxStarted, applyCount, releaseCount,
                 earlyApply, unsafeStopCompletion, appliedInsideStopIrq,
                 oldTxRetryPending>>

EndLaunch ==
  /\ phase = "Launching"
  /\ launchReturned = Dirs
  /\ armResolved = Dirs
  /\ phase' =
       IF (abortPending = {}) /\ (asyncStopArmed = {})
         THEN "Quiescent"
         ELSE "Stopping"
  /\ UNCHANGED <<dmaEnabled, stopRequested, launchReturned, armResolved,
                 abortPending, asyncStopArmed, stopIrqRunning,
                 stopIrqChecked, configBoundary, rxRunning, writeQueued,
                 newTxStarted, applyCount, releaseCount, earlyApply,
                 unsafeStopCompletion, appliedInsideStopIrq,
                 oldTxRetryPending>>

(***************************************************************************)
(* An armed IRQ is only a wakeup. If EN remains set, both obligations stay  *)
(* durable and another IRQ may retry after hardware stops. If EN is clear,  *)
(* the IRQ may publish Quiescent. Applying CONFIG before this IRQ returns is *)
(* intentionally allowed because StopIrqCheck is its final old-state access. *)
(***************************************************************************)

StopIrqEnter(d) ==
  /\ phase \in {"Launching", "Stopping"}
  /\ d \in asyncStopArmed
  /\ d \notin stopIrqRunning
  /\ stopIrqRunning' = stopIrqRunning \cup {d}
  /\ stopIrqChecked' = stopIrqChecked \ {d}
  /\ UNCHANGED <<phase, dmaEnabled, stopRequested, launchReturned,
                 armResolved, abortPending, asyncStopArmed, configBoundary,
                 rxRunning, writeQueued, newTxStarted, applyCount,
                 releaseCount, earlyApply, unsafeStopCompletion,
                 appliedInsideStopIrq, oldTxRetryPending>>

StopIrqCheck(d) ==
  /\ d \in stopIrqRunning
  /\ d \notin stopIrqChecked
  /\ LET mayComplete == (d \notin dmaEnabled) \/ BreakStopWithoutProof
         nextPending ==
           IF mayComplete THEN abortPending \ {d} ELSE abortPending
         nextArmed ==
           IF mayComplete THEN asyncStopArmed \ {d} ELSE asyncStopArmed
     IN /\ abortPending' = nextPending
        /\ asyncStopArmed' = nextArmed
        /\ phase' =
             IF /\ phase = "Stopping"
                /\ nextPending = {}
                /\ nextArmed = {}
               THEN "Quiescent"
               ELSE phase
  /\ stopIrqChecked' = stopIrqChecked \cup {d}
  /\ unsafeStopCompletion' =
       (unsafeStopCompletion \/
         (BreakStopWithoutProof /\ (d \in dmaEnabled)))
  /\ UNCHANGED <<dmaEnabled, stopRequested, launchReturned, armResolved,
                 stopIrqRunning, configBoundary, rxRunning, writeQueued,
                 newTxStarted, applyCount, releaseCount, earlyApply,
                 appliedInsideStopIrq, oldTxRetryPending>>

StopIrqExit(d) ==
  /\ d \in stopIrqRunning
  /\ d \in stopIrqChecked
  /\ stopIrqRunning' = stopIrqRunning \ {d}
  /\ stopIrqChecked' = stopIrqChecked \ {d}
  /\ UNCHANGED <<phase, dmaEnabled, stopRequested, launchReturned,
                 armResolved, abortPending, asyncStopArmed, configBoundary,
                 rxRunning, writeQueued, newTxStarted, applyCount,
                 releaseCount, earlyApply, unsafeStopCompletion,
                 appliedInsideStopIrq, oldTxRetryPending>>

WriteDuringConfig ==
  /\ phase \in {"Launching", "Stopping", "Quiescent", "Applied"}
  /\ ~writeQueued
  /\ writeQueued' = TRUE
  /\ UNCHANGED <<phase, dmaEnabled, stopRequested, launchReturned,
                 armResolved, abortPending, asyncStopArmed, stopIrqRunning,
                 stopIrqChecked, configBoundary, rxRunning, newTxStarted,
                 applyCount, releaseCount, earlyApply,
                 unsafeStopCompletion, appliedInsideStopIrq,
                 oldTxRetryPending>>

ApplyConfig ==
  /\ applyCount = 0
  /\ IF BreakEarlyJoin
        THEN phase \in {"Launching", "Stopping", "Quiescent"}
        ELSE phase = "Quiescent"
  /\ phase' = "Applied"
  /\ rxRunning' = TRUE
  /\ applyCount' = 1
  /\ earlyApply' =
       (earlyApply \/
         (launchReturned # Dirs) \/
         (armResolved # Dirs) \/
         (abortPending # {}) \/
         (asyncStopArmed # {}) \/
         (dmaEnabled # {}) \/
         ~(stopIrqRunning \subseteq stopIrqChecked))
  /\ appliedInsideStopIrq' =
       (appliedInsideStopIrq \/ (stopIrqRunning # {}))
  /\ UNCHANGED <<dmaEnabled, stopRequested, launchReturned, armResolved,
                 abortPending, asyncStopArmed, stopIrqRunning,
                 stopIrqChecked, configBoundary, writeQueued, newTxStarted,
                 releaseCount, unsafeStopCompletion, oldTxRetryPending>>

DrainAndRelease ==
  /\ phase = "Applied"
  /\ releaseCount = 0
  /\ phase' = "Released"
  /\ configBoundary' = FALSE
  /\ releaseCount' = 1
  /\ newTxStarted' = IF BreakNoWriteRescan THEN FALSE ELSE writeQueued
  /\ UNCHANGED <<dmaEnabled, stopRequested, launchReturned, armResolved,
                 abortPending, asyncStopArmed, stopIrqRunning,
                 stopIrqChecked, rxRunning, writeQueued, applyCount,
                 earlyApply, unsafeStopCompletion, appliedInsideStopIrq,
                 oldTxRetryPending>>

Next ==
  \/ RequestConfig
  \/ \E d \in Dirs: LaunchStop(d)
  \/ \E d \in Dirs: HardwareStop(d)
  \/ \E d \in Dirs: ResolveAsyncStop(d)
  \/ EndLaunch
  \/ \E d \in Dirs: StopIrqEnter(d)
  \/ \E d \in Dirs: StopIrqCheck(d)
  \/ \E d \in Dirs: StopIrqExit(d)
  \/ WriteDuringConfig
  \/ ApplyConfig
  \/ DrainAndRelease

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ phase \in Phases
  /\ dmaEnabled \subseteq Dirs
  /\ stopRequested \subseteq Dirs
  /\ launchReturned \subseteq Dirs
  /\ armResolved \subseteq Dirs
  /\ abortPending \subseteq Dirs
  /\ asyncStopArmed \subseteq Dirs
  /\ stopIrqRunning \subseteq Dirs
  /\ stopIrqChecked \subseteq Dirs
  /\ oldTxRetryPending \in BOOLEAN
  /\ applyCount \in 0..1
  /\ releaseCount \in 0..1

ArmedDirectionsRemainPending == asyncStopArmed \subseteq abortPending

NoEarlyApply == ~earlyApply

StopCompletionRequiresDisabled == ~unsafeStopCompletion

QuiescentRequiresStopped ==
  phase \in {"Quiescent", "Applied", "Released"} =>
    /\ launchReturned = Dirs
    /\ armResolved = Dirs
    /\ abortPending = {}
    /\ asyncStopArmed = {}
    /\ dmaEnabled = {}

ApplyAndReleaseAtMostOnce ==
  /\ releaseCount <= applyCount
  /\ applyCount <= 1
  /\ releaseCount <= 1

ConfigOwnsUntilDrain ==
  configBoundary = FALSE => phase \in {"Idle", "Released"}

RxBlackoutUntilJoin ==
  phase \in {"Launching", "Stopping", "Quiescent"} => ~rxRunning

NoTxStartBeforeRelease == newTxStarted => phase = "Released"

PostConfigWriteRescanned ==
  phase = "Released" /\ writeQueued => newTxStarted

NoRetiredTxRetryAfterConfig ==
  phase = "Released" => ~oldTxRetryPending

(***************************************************************************)
(* Witness property: the corrected protocol intentionally permits CONFIG   *)
(* apply while the final stop IRQ remains on the stack, after its EN proof. *)
(***************************************************************************)
NoInsideStopIrqApplyWitness == ~appliedInsideStopIrq

=============================================================================
