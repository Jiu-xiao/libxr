------------------------ MODULE UartNestedTxAdmission ------------------------
EXTENDS Naturals, TLC

(***************************************************************************
 * Focused IRQ-owner nested TX-start admission protocol.
 *
 * The model starts after an IRQ invocation has acquired the hardware gate and
 * synchronously won the TX service owner.  The service event has already been
 * consumed, so an unstarted candidate needs either the still-active outer IRQ
 * call stack or a durable TX retry bit to carry it forward.
 *
 * Correct admission is one atomic action.  A CONFIG or deferred-IRQ publication
 * ordered before it blocks the nested start; a publication ordered after it
 * waits for the outer IRQ owner.  A later CONFIG claim retires the blocked old
 * candidate and its retry in one action.  The intentionally broken mode splits
 * the priority check from admission so each high-priority publisher can occupy
 * the resulting window.
 *************************************************************************)

CONSTANTS AtomicNestedAdmission,
          DurableFallback

ASSUME /\ AtomicNestedAdmission \in BOOLEAN
       /\ DurableFallback \in BOOLEAN

Owners == {"None", "IRQ", "CONFIG", "DEFERRED", "TX"}
TxPcs == {"Ready", "Checked", "Blocked", "Nested", "NestedStarted",
           "Retry", "Fallback", "Done", "Cancelled"}

VARIABLES owner,
          contextValid,
          contextDepth,
          txPc,
          txPending,
          configPending,
          deferredPending,
          configPublished,
          deferredPublished,
          nestedAdmissions,
          txStarts

vars == <<owner, contextValid, contextDepth, txPc, txPending,
          configPending, deferredPending, configPublished,
          deferredPublished, nestedAdmissions, txStarts>>

Init ==
  /\ owner = "IRQ"
  /\ contextValid = TRUE
  /\ contextDepth = 1
  /\ txPc = "Ready"
  /\ txPending \in BOOLEAN
  /\ configPending = FALSE
  /\ deferredPending = FALSE
  /\ configPublished = FALSE
  /\ deferredPublished = FALSE
  /\ nestedAdmissions = 0
  /\ txStarts = 0

PublishConfig ==
  /\ ~configPublished
  /\ configPublished' = TRUE
  /\ configPending' = TRUE
  /\ UNCHANGED <<owner, contextValid, contextDepth, txPc, txPending,
                  deferredPending, deferredPublished, nestedAdmissions,
                  txStarts>>

PublishDeferred ==
  /\ ~deferredPublished
  /\ deferredPublished' = TRUE
  /\ deferredPending' = TRUE
  /\ UNCHANGED <<owner, contextValid, contextDepth, txPc, txPending,
                  configPending, configPublished, nestedAdmissions,
                  txStarts>>

(***************************************************************************
 * Correct nested admission.  The action is the abstract linearization point
 * of the successful same-word CAS.  Clearing txPending in the same action
 * models consumption of a retry left by an earlier blocked start.
 *************************************************************************)
NestedAdmitAtomic ==
  /\ AtomicNestedAdmission
  /\ txPc = "Ready"
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 1
  /\ ~configPending
  /\ ~deferredPending
  /\ txPc' = "Nested"
  /\ contextDepth' = 2
  /\ txPending' = FALSE
  /\ nestedAdmissions' = nestedAdmissions + 1
  /\ UNCHANGED <<owner, contextValid, configPending, deferredPending,
                  configPublished, deferredPublished, txStarts>>

(***************************************************************************
 * Broken check/commit pair.  NestedCheck records that priority bits were clear,
 * but NestedCommitBroken does not revalidate them.  CONFIG or deferred work may
 * therefore publish between the two actions.
 *************************************************************************)
NestedCheck ==
  /\ ~AtomicNestedAdmission
  /\ txPc = "Ready"
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 1
  /\ ~configPending
  /\ ~deferredPending
  /\ txPc' = "Checked"
  /\ UNCHANGED <<owner, contextValid, contextDepth, txPending,
                  configPending, deferredPending, configPublished,
                  deferredPublished, nestedAdmissions, txStarts>>

NestedCommitBroken ==
  /\ ~AtomicNestedAdmission
  /\ txPc = "Checked"
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 1
  /\ txPc' = "Nested"
  /\ contextDepth' = 2
  /\ txPending' = FALSE
  /\ nestedAdmissions' = nestedAdmissions + 1
  /\ UNCHANGED <<owner, contextValid, configPending, deferredPending,
                  configPublished, deferredPublished, txStarts>>

NestedObserveBlocked ==
  /\ txPc = "Ready"
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 1
  /\ (configPending \/ deferredPending)
  /\ txPc' = "Blocked"
  /\ UNCHANGED <<owner, contextValid, contextDepth, txPending,
                  configPending, deferredPending, configPublished,
                  deferredPublished, nestedAdmissions, txStarts>>

(***************************************************************************
 * The outer IRQ cannot leave between observing the block and publishing the
 * retry: both actions execute in one synchronous call stack.  DurableFallback
 * FALSE models returning RETRY without creating that persistent obligation.
 *************************************************************************)
PublishTxRetry ==
  /\ txPc = "Blocked"
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 1
  /\ txPc' = "Retry"
  /\ txPending' = IF DurableFallback THEN TRUE ELSE txPending
  /\ UNCHANGED <<owner, contextValid, contextDepth, configPending,
                  deferredPending, configPublished, deferredPublished,
                  nestedAdmissions, txStarts>>

NestedStart ==
  /\ txPc = "Nested"
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 2
  /\ txPc' = "NestedStarted"
  /\ txStarts' = txStarts + 1
  /\ UNCHANGED <<owner, contextValid, contextDepth, txPending,
                  configPending, deferredPending, configPublished,
                  deferredPublished, nestedAdmissions>>

NestedLeave ==
  /\ txPc = "NestedStarted"
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 2
  /\ txPc' = "Done"
  /\ contextDepth' = 1
  /\ UNCHANGED <<owner, contextValid, txPending, configPending,
                  deferredPending, configPublished, deferredPublished,
                  nestedAdmissions, txStarts>>

OuterIrqLeave ==
  /\ owner = "IRQ"
  /\ contextValid
  /\ contextDepth = 1
  /\ txPc \in {"Retry", "Done"}
  /\ owner' = "None"
  /\ contextValid' = FALSE
  /\ contextDepth' = 0
  /\ UNCHANGED <<txPc, txPending, configPending, deferredPending,
                  configPublished, deferredPublished, nestedAdmissions,
                  txStarts>>

(***************************************************************************
 * CONFIG claim atomically retires a retry for the old fixed prefix.        *
 * Cancelled means ownership has irreversibly moved to the CONFIG drain; it *
 * does not mean the operation callback has already executed.               *
 *************************************************************************)
ConfigClaim ==
  /\ owner = "None"
  /\ configPending
  /\ owner' = "CONFIG"
  /\ configPending' = FALSE
  /\ txPending' = FALSE
  /\ txPc' = IF txPc = "Retry" THEN "Cancelled" ELSE txPc
  /\ UNCHANGED <<contextValid, contextDepth,
                  deferredPending, configPublished, deferredPublished,
                  nestedAdmissions, txStarts>>

ConfigLeave ==
  /\ owner = "CONFIG"
  /\ owner' = "None"
  /\ UNCHANGED <<contextValid, contextDepth, txPc, txPending,
                  configPending, deferredPending, configPublished,
                  deferredPublished, nestedAdmissions, txStarts>>

DeferredClaim ==
  /\ owner = "None"
  /\ ~configPending
  /\ deferredPending
  /\ owner' = "DEFERRED"
  /\ deferredPending' = FALSE
  /\ UNCHANGED <<contextValid, contextDepth, txPc, txPending,
                  configPending, configPublished, deferredPublished,
                  nestedAdmissions, txStarts>>

DeferredLeave ==
  /\ owner = "DEFERRED"
  /\ owner' = "None"
  /\ UNCHANGED <<contextValid, contextDepth, txPc, txPending,
                  configPending, deferredPending, configPublished,
                  deferredPublished, nestedAdmissions, txStarts>>

FallbackClaim ==
  /\ owner = "None"
  /\ ~configPending
  /\ ~deferredPending
  /\ txPc = "Retry"
  /\ txPending
  /\ owner' = "TX"
  /\ txPending' = FALSE
  /\ txPc' = "Fallback"
  /\ UNCHANGED <<contextValid, contextDepth, configPending,
                  deferredPending, configPublished, deferredPublished,
                  nestedAdmissions, txStarts>>

FallbackStart ==
  /\ owner = "TX"
  /\ txPc = "Fallback"
  /\ txStarts = 0
  /\ owner' = "None"
  /\ txPc' = "Done"
  /\ txStarts' = 1
  /\ UNCHANGED <<contextValid, contextDepth, txPending, configPending,
                  deferredPending, configPublished, deferredPublished,
                  nestedAdmissions>>

NestedAttempt == NestedAdmitAtomic \/ NestedCheck \/ NestedObserveBlocked

Next ==
  \/ PublishConfig
  \/ PublishDeferred
  \/ NestedAttempt
  \/ NestedCommitBroken
  \/ PublishTxRetry
  \/ NestedStart
  \/ NestedLeave
  \/ OuterIrqLeave
  \/ ConfigClaim
  \/ ConfigLeave
  \/ DeferredClaim
  \/ DeferredLeave
  \/ FallbackClaim
  \/ FallbackStart

Spec == Init /\ [][Next]_vars

FairSpec ==
  /\ Spec
  /\ WF_vars(PublishConfig)
  /\ WF_vars(PublishDeferred)
  /\ WF_vars(NestedAttempt)
  /\ WF_vars(NestedCommitBroken)
  /\ WF_vars(PublishTxRetry)
  /\ WF_vars(NestedStart)
  /\ WF_vars(NestedLeave)
  /\ WF_vars(OuterIrqLeave)
  /\ WF_vars(ConfigClaim)
  /\ WF_vars(ConfigLeave)
  /\ WF_vars(DeferredClaim)
  /\ WF_vars(DeferredLeave)
  /\ WF_vars(FallbackClaim)
  /\ WF_vars(FallbackStart)

TypeOK ==
  /\ owner \in Owners
  /\ contextValid \in BOOLEAN
  /\ contextDepth \in 0..2
  /\ txPc \in TxPcs
  /\ txPending \in BOOLEAN
  /\ configPending \in BOOLEAN
  /\ deferredPending \in BOOLEAN
  /\ configPublished \in BOOLEAN
  /\ deferredPublished \in BOOLEAN
  /\ nestedAdmissions \in 0..1
  /\ txStarts \in 0..1

OwnerContextScoped == contextValid <=> owner = "IRQ"

NestedDepthWellFormed ==
  /\ (contextValid => contextDepth \in 1..2)
  /\ (~contextValid => contextDepth = 0)
  /\ (contextDepth = 2) <=> txPc \in {"Nested", "NestedStarted"}

NestedStartAtMostOnce == txStarts <= 1

RetryDoesNotStart == txPc = "Retry" => txStarts = 0

CancelledDoesNotStart == txPc = "Cancelled" => txStarts = 0

(***************************************************************************
 * After TryEnterNestedTxStart has returned RETRY, either the outer IRQ stack
 * has not released yet or TX_PENDING/normal TX ownership carries the request.
 *************************************************************************)
BlockedTxHasCarrier ==
  txPc # "Retry" \/ owner = "IRQ" \/ txPending \/ owner = "TX"

NestedAdmissionStep == nestedAdmissions' = nestedAdmissions + 1

NestedAdmissionRequiresIrqContextStep ==
  NestedAdmissionStep =>
    (owner = "IRQ" /\ contextValid /\ contextDepth = 1)

NestedAdmissionConsumesTxPendingStep ==
  NestedAdmissionStep => ~txPending'

NestedAdmissionRespectsConfigPriorityStep ==
  NestedAdmissionStep => ~configPending

NestedAdmissionRespectsDeferredPriorityStep ==
  NestedAdmissionStep => ~deferredPending

OuterIrqReleaseRequiresBaseDepthStep ==
  (owner = "IRQ" /\ owner' # "IRQ") => contextDepth = 1

ConfigAdmissionRetiresTxPendingStep ==
  (owner = "None" /\ owner' = "CONFIG") => ~txPending'

NestedAdmissionRequiresIrqContext ==
  [][NestedAdmissionRequiresIrqContextStep]_vars

NestedAdmissionConsumesTxPending ==
  [][NestedAdmissionConsumesTxPendingStep]_vars

NestedAdmissionRespectsConfigPriority ==
  [][NestedAdmissionRespectsConfigPriorityStep]_vars

NestedAdmissionRespectsDeferredPriority ==
  [][NestedAdmissionRespectsDeferredPriorityStep]_vars

OuterIrqReleaseRequiresBaseDepth ==
  [][OuterIrqReleaseRequiresBaseDepthStep]_vars

ConfigAdmissionRetiresTxPending ==
  [][ConfigAdmissionRetiresTxPendingStep]_vars

TxEventuallySettles ==
  (txPc = "Ready") ~> (txPc \in {"Done", "Cancelled"})

TxPendingEventuallyConsumed == txPending ~> ~txPending

IrqContextEventuallyReleased == contextValid ~> ~contextValid

=============================================================================
