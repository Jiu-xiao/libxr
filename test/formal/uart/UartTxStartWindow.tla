------------------------- MODULE UartTxStartWindow -------------------------
EXTENDS Naturals, FiniteSets, TLC

(***************************************************************************
 * Focused composition model for the interval after a backend has enabled
 * TX DMA but before UartDmaTxModel commits software ownership.
 *
 * The TX service remains the sole software-state owner throughout this
 * interval.  Releasing the hardware gate allows CONFIG and IRQ paths to run,
 * but those paths may only publish coalesced service events.  They may not
 * apply CONFIG, release active ownership, or advance the TX state directly.
 *************************************************************************)

CONSTANTS BreakEarlyDirectMutation,
          BreakDoubleTerminalAdvance,
          BreakMissingFailureConfig

ASSUME /\ BreakEarlyDirectMutation \in BOOLEAN
       /\ BreakDoubleTerminalAdvance \in BOOLEAN
       /\ BreakMissingFailureConfig \in BOOLEAN

ServiceEvents == {"CONFIG", "COMPLETE", "ERROR"}
ServicePhases == {"Idle", "Candidate", "Starting", "ReleaseGate",
                  "Commit", "FailureResolve", "FailureCallback",
                  "FailureReturn", "Take", "Handle", "Done"}
GateOwners == {"None", "TX"}
BackendResults == {"None", "Started", "Failed"}

VARIABLES serviceOwner,
          servicePhase,
          events,
          snapshot,
          gateOwner,
          gateReleased,
          startIssued,
          dmaEnabled,
          backendResult,
          softwareCommitted,
          activeOwned,
          configApplyCount,
          activeReleaseCount,
          terminalAdvanceCount,
          failureSeen,
          configPublished,
          completePublished,
          errorPublished,
          handledErrorAndComplete

vars == <<serviceOwner, servicePhase, events, snapshot, gateOwner,
          gateReleased, startIssued, dmaEnabled, backendResult,
          softwareCommitted, activeOwned, configApplyCount,
          activeReleaseCount, terminalAdvanceCount, failureSeen,
          configPublished, completePublished, errorPublished,
          handledErrorAndComplete>>

Init ==
  /\ serviceOwner = FALSE
  /\ servicePhase = "Idle"
  /\ events = {}
  /\ snapshot = {}
  /\ gateOwner = "None"
  /\ gateReleased = FALSE
  /\ startIssued = FALSE
  /\ dmaEnabled = FALSE
  /\ backendResult = "None"
  /\ softwareCommitted = FALSE
  /\ activeOwned = FALSE
  /\ configApplyCount = 0
  /\ activeReleaseCount = 0
  /\ terminalAdvanceCount = 0
  /\ failureSeen = FALSE
  /\ configPublished = FALSE
  /\ completePublished = FALSE
  /\ errorPublished = FALSE
  /\ handledErrorAndComplete = FALSE

ServiceClaim ==
  /\ ~serviceOwner
  /\ servicePhase = "Idle"
  /\ serviceOwner' = TRUE
  /\ servicePhase' = "Candidate"
  /\ UNCHANGED <<events, snapshot, gateOwner, gateReleased, startIssued,
                  dmaEnabled, backendResult, softwareCommitted, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  failureSeen, configPublished, completePublished,
                  errorPublished, handledErrorAndComplete>>

TxGateClaim ==
  /\ serviceOwner
  /\ servicePhase = "Candidate"
  /\ gateOwner = "None"
  /\ gateOwner' = "TX"
  /\ servicePhase' = "Starting"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateReleased, startIssued,
                  dmaEnabled, backendResult, softwareCommitted, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  failureSeen, configPublished, completePublished,
                  errorPublished, handledErrorAndComplete>>

BackendStarted ==
  /\ serviceOwner
  /\ servicePhase = "Starting"
  /\ gateOwner = "TX"
  /\ backendResult = "None"
  /\ backendResult' = "Started"
  /\ startIssued' = TRUE
  /\ dmaEnabled' = TRUE
  /\ servicePhase' = "ReleaseGate"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateOwner, gateReleased,
                  softwareCommitted, activeOwned, configApplyCount,
                  activeReleaseCount, terminalAdvanceCount, failureSeen,
                  configPublished, completePublished, errorPublished,
                  handledErrorAndComplete>>

BackendFailed ==
  /\ serviceOwner
  /\ servicePhase = "Starting"
  /\ gateOwner = "TX"
  /\ backendResult = "None"
  /\ backendResult' = "Failed"
  /\ failureSeen' = TRUE
  /\ servicePhase' = "ReleaseGate"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateOwner, gateReleased,
                  startIssued, dmaEnabled, softwareCommitted, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  configPublished, completePublished, errorPublished,
                  handledErrorAndComplete>>

TxGateRelease ==
  /\ serviceOwner
  /\ servicePhase = "ReleaseGate"
  /\ gateOwner = "TX"
  /\ gateOwner' = "None"
  /\ gateReleased' = TRUE
  /\ servicePhase' = "Commit"
  /\ UNCHANGED <<events, snapshot, serviceOwner, startIssued, dmaEnabled,
                  backendResult, softwareCommitted, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  failureSeen, configPublished, completePublished,
                  errorPublished, handledErrorAndComplete>>

(***************************************************************************
 * These external publishers run after TX released the hardware gate.  The
 * focused STARTED path publishes before software commit; CONFIG may also be
 * published while a failed result is delivered.  In the correct protocol
 * their only effect is to add one durable service event.
 *************************************************************************)

PublishConfig ==
  /\ serviceOwner
  /\ servicePhase \in
       {"Commit", "FailureResolve", "FailureCallback", "FailureReturn"}
  /\ gateOwner = "None"
  /\ gateReleased
  /\ (servicePhase = "Commit" => ~softwareCommitted)
  /\ (servicePhase # "Commit" => softwareCommitted /\ failureSeen)
  /\ ~configPublished
  /\ events' = events \union {"CONFIG"}
  /\ configPublished' = TRUE
  /\ configApplyCount' = configApplyCount +
       (IF BreakEarlyDirectMutation THEN 1 ELSE 0)
  /\ dmaEnabled' = IF BreakEarlyDirectMutation THEN FALSE ELSE dmaEnabled
  /\ UNCHANGED <<snapshot, serviceOwner, servicePhase, gateOwner,
                  gateReleased, startIssued, backendResult, softwareCommitted,
                  activeOwned, activeReleaseCount, terminalAdvanceCount,
                  failureSeen, completePublished, errorPublished,
                  handledErrorAndComplete>>

PublishComplete ==
  /\ serviceOwner
  /\ servicePhase = "Commit"
  /\ gateOwner = "None"
  /\ gateReleased
  /\ startIssued
  /\ ~softwareCommitted
  /\ ~completePublished
  /\ events' = events \union {"COMPLETE"}
  /\ completePublished' = TRUE
  /\ activeReleaseCount' = activeReleaseCount +
       (IF BreakEarlyDirectMutation THEN 1 ELSE 0)
  /\ dmaEnabled' = IF BreakEarlyDirectMutation THEN FALSE ELSE dmaEnabled
  /\ UNCHANGED <<snapshot, serviceOwner, servicePhase, gateOwner,
                  gateReleased, startIssued, backendResult, softwareCommitted,
                  activeOwned, configApplyCount, terminalAdvanceCount,
                  failureSeen, configPublished, errorPublished,
                  handledErrorAndComplete>>

PublishError ==
  /\ serviceOwner
  /\ servicePhase = "Commit"
  /\ gateOwner = "None"
  /\ gateReleased
  /\ startIssued
  /\ ~softwareCommitted
  /\ ~errorPublished
  /\ events' = events \union {"ERROR"}
  /\ errorPublished' = TRUE
  /\ activeReleaseCount' = activeReleaseCount +
       (IF BreakEarlyDirectMutation THEN 1 ELSE 0)
  /\ dmaEnabled' = IF BreakEarlyDirectMutation THEN FALSE ELSE dmaEnabled
  /\ UNCHANGED <<snapshot, serviceOwner, servicePhase, gateOwner,
                  gateReleased, startIssued, backendResult, softwareCommitted,
                  activeOwned, configApplyCount, terminalAdvanceCount,
                  failureSeen, configPublished, completePublished,
                  handledErrorAndComplete>>

CommitStarted ==
  /\ serviceOwner
  /\ servicePhase = "Commit"
  /\ gateOwner = "None"
  /\ gateReleased
  /\ backendResult = "Started"
  /\ startIssued
  /\ ~softwareCommitted
  /\ softwareCommitted' = TRUE
  /\ activeOwned' = TRUE
  /\ servicePhase' = "Take"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateOwner, gateReleased,
                  startIssued, dmaEnabled, backendResult, configApplyCount,
                  activeReleaseCount, terminalAdvanceCount, failureSeen,
                  configPublished, completePublished, errorPublished,
                  handledErrorAndComplete>>

CommitFailedState ==
  /\ serviceOwner
  /\ servicePhase = "Commit"
  /\ gateOwner = "None"
  /\ gateReleased
  /\ backendResult = "Failed"
  /\ failureSeen
  /\ ~softwareCommitted
  /\ softwareCommitted' = TRUE
  /\ servicePhase' = "FailureResolve"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateOwner, gateReleased,
                  startIssued, dmaEnabled, backendResult, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  failureSeen, configPublished, completePublished,
                  errorPublished, handledErrorAndComplete>>

(***************************************************************************
 * A failed start has already committed queue/pending ownership at this
 * point, but RequestConfig() has not run yet.  The service owner and these
 * explicit PCs are the durable local continuation across direct result
 * delivery or a contextless WritePort::Finish() callback.
 *************************************************************************)

FailureResolveDirect ==
  /\ serviceOwner
  /\ servicePhase = "FailureResolve"
  /\ failureSeen
  /\ softwareCommitted
  /\ servicePhase' = "FailureReturn"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateOwner, gateReleased,
                  startIssued, dmaEnabled, backendResult, softwareCommitted,
                  activeOwned, configApplyCount, activeReleaseCount,
                  terminalAdvanceCount, failureSeen,
                  configPublished, completePublished, errorPublished,
                  handledErrorAndComplete>>

FailureEnterCallback ==
  /\ serviceOwner
  /\ servicePhase = "FailureResolve"
  /\ failureSeen
  /\ softwareCommitted
  /\ servicePhase' = "FailureCallback"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateOwner, gateReleased,
                  startIssued, dmaEnabled, backendResult, softwareCommitted,
                  activeOwned, configApplyCount, activeReleaseCount,
                  terminalAdvanceCount, failureSeen,
                  configPublished, completePublished, errorPublished,
                  handledErrorAndComplete>>

FailureReturnFromCallback ==
  /\ serviceOwner
  /\ servicePhase = "FailureCallback"
  /\ failureSeen
  /\ softwareCommitted
  /\ servicePhase' = "FailureReturn"
  /\ UNCHANGED <<events, snapshot, serviceOwner, gateOwner, gateReleased,
                  startIssued, dmaEnabled, backendResult, softwareCommitted,
                  activeOwned, configApplyCount, activeReleaseCount,
                  terminalAdvanceCount, failureSeen,
                  configPublished, completePublished, errorPublished,
                  handledErrorAndComplete>>

PublishFailureConfig ==
  /\ serviceOwner
  /\ servicePhase = "FailureReturn"
  /\ failureSeen
  /\ softwareCommitted
  /\ events' = IF BreakMissingFailureConfig
                THEN events
                ELSE events \union {"CONFIG"}
  /\ servicePhase' = "Take"
  /\ UNCHANGED <<snapshot, serviceOwner, gateOwner, gateReleased, startIssued,
                  dmaEnabled, backendResult, softwareCommitted, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  failureSeen,
                  configPublished, completePublished, errorPublished,
                  handledErrorAndComplete>>

ServiceTake ==
  /\ serviceOwner
  /\ servicePhase = "Take"
  /\ softwareCommitted
  /\ snapshot = {}
  /\ events # {}
  /\ snapshot' = events
  /\ events' = {}
  /\ servicePhase' = "Handle"
  /\ UNCHANGED <<serviceOwner, gateOwner, gateReleased, startIssued,
                  dmaEnabled, backendResult, softwareCommitted, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  failureSeen, configPublished, completePublished,
                  errorPublished, handledErrorAndComplete>>

ServiceHandle ==
  LET hasConfig == "CONFIG" \in snapshot
      hasError == "ERROR" \in snapshot
      hasComplete == "COMPLETE" \in snapshot
      terminalSelected == ~hasConfig /\ (hasError \/ hasComplete)
      bothTerminals == terminalSelected /\ hasError /\ hasComplete
      terminalDelta ==
        IF ~terminalSelected THEN 0
        ELSE IF bothTerminals /\ BreakDoubleTerminalAdvance THEN 2
        ELSE 1
      releasesActive == activeOwned /\ (hasConfig \/ terminalSelected)
  IN
  /\ serviceOwner
  /\ servicePhase = "Handle"
  /\ softwareCommitted
  /\ snapshot # {}
  /\ configApplyCount' = configApplyCount + (IF hasConfig THEN 1 ELSE 0)
  /\ activeReleaseCount' = activeReleaseCount + (IF releasesActive THEN 1 ELSE 0)
  /\ terminalAdvanceCount' = terminalAdvanceCount + terminalDelta
  /\ activeOwned' = IF hasConfig \/ terminalSelected THEN FALSE ELSE activeOwned
  /\ dmaEnabled' = IF hasConfig \/ terminalSelected THEN FALSE ELSE dmaEnabled
  /\ handledErrorAndComplete' =
       (handledErrorAndComplete \/ bothTerminals)
  /\ snapshot' = {}
  /\ servicePhase' = "Take"
  /\ UNCHANGED <<events, serviceOwner, gateOwner, gateReleased, startIssued,
                  backendResult, softwareCommitted, failureSeen,
                  configPublished, completePublished, errorPublished>>

ServiceRelease ==
  /\ serviceOwner
  /\ servicePhase = "Take"
  /\ snapshot = {}
  /\ events = {}
  /\ serviceOwner' = FALSE
  /\ servicePhase' = "Done"
  /\ UNCHANGED <<events, snapshot, gateOwner, gateReleased, startIssued,
                  dmaEnabled, backendResult, softwareCommitted, activeOwned,
                  configApplyCount, activeReleaseCount, terminalAdvanceCount,
                  failureSeen, configPublished, completePublished,
                  errorPublished, handledErrorAndComplete>>

Next ==
  \/ ServiceClaim
  \/ TxGateClaim
  \/ BackendStarted
  \/ BackendFailed
  \/ TxGateRelease
  \/ PublishConfig
  \/ PublishComplete
  \/ PublishError
  \/ CommitStarted
  \/ CommitFailedState
  \/ FailureResolveDirect
  \/ FailureEnterCallback
  \/ FailureReturnFromCallback
  \/ PublishFailureConfig
  \/ ServiceTake
  \/ ServiceHandle
  \/ ServiceRelease

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ serviceOwner \in BOOLEAN
  /\ servicePhase \in ServicePhases
  /\ events \subseteq ServiceEvents
  /\ snapshot \subseteq ServiceEvents
  /\ gateOwner \in GateOwners
  /\ gateReleased \in BOOLEAN
  /\ startIssued \in BOOLEAN
  /\ dmaEnabled \in BOOLEAN
  /\ backendResult \in BackendResults
  /\ softwareCommitted \in BOOLEAN
  /\ activeOwned \in BOOLEAN
  /\ configApplyCount \in 0..4
  /\ activeReleaseCount \in 0..4
  /\ terminalAdvanceCount \in 0..4
  /\ failureSeen \in BOOLEAN
  /\ configPublished \in BOOLEAN
  /\ completePublished \in BOOLEAN
  /\ errorPublished \in BOOLEAN
  /\ handledErrorAndComplete \in BOOLEAN

ServiceOwnerConsistent ==
  serviceOwner <=> servicePhase \notin {"Idle", "Done"}

GateOwnerConsistent ==
  gateOwner = "TX" =>
    serviceOwner /\ servicePhase \in {"Starting", "ReleaseGate"}

SoftwareCommitFollowsGateRelease == ~softwareCommitted \/ gateReleased

StartStateConsistent ==
  /\ (startIssued <=> backendResult = "Started")
  /\ (dmaEnabled => startIssued)
  /\ (activeOwned => softwareCommitted /\ backendResult = "Started")
  /\ (failureSeen <=> backendResult = "Failed")

PublishedEventsFollowGateRelease ==
  ~(configPublished \/ completePublished \/ errorPublished) \/ gateReleased

FailureCommitObligationHasCarrier ==
  ~failureSeen \/
    "CONFIG" \in events \/
    "CONFIG" \in snapshot \/
    configApplyCount > 0 \/
    (serviceOwner /\
     servicePhase \in {"ReleaseGate", "Commit", "FailureResolve",
                       "FailureCallback", "FailureReturn"})

(***************************************************************************
 * Independent fault-sensitive properties.
 *************************************************************************)

ExternalPathsArePublishOnly ==
  servicePhase \notin
    {"Commit", "FailureResolve", "FailureCallback", "FailureReturn"} \/
    (configApplyCount = 0 /\ activeReleaseCount = 0 /\
     terminalAdvanceCount = 0)

ErrorAbsorbsComplete ==
  ~handledErrorAndComplete \/ terminalAdvanceCount = 1

TerminalAdvancesAtMostOnce == terminalAdvanceCount <= 1

FailedHasConfigCarrier ==
  ~(failureSeen /\ softwareCommitted) \/
    "CONFIG" \in events \/
    "CONFIG" \in snapshot \/
    configApplyCount > 0 \/
    (serviceOwner /\
     servicePhase \in {"FailureResolve", "FailureCallback", "FailureReturn"})

(***************************************************************************
 * Expected-failing reachability witnesses.  Each runner configuration
 * checks one of these alone so a guard regression cannot silently remove
 * the central post-release/pre-commit schedules.
 *************************************************************************)

NoStartedPreCommitWindowWitness ==
  ~(serviceOwner /\
    servicePhase = "Commit" /\
    gateOwner = "None" /\
    gateReleased /\
    backendResult = "Started" /\
    startIssued /\
    dmaEnabled /\
    ~softwareCommitted)

NoFailedConfigPreCommitWindowWitness ==
  ~(serviceOwner /\
    servicePhase = "Commit" /\
    gateOwner = "None" /\
    gateReleased /\
    backendResult = "Failed" /\
    failureSeen /\
    ~softwareCommitted /\
    configPublished /\
    "CONFIG" \in events)

NoFailedCallbackPreConfigWindowWitness ==
  ~(serviceOwner /\
    servicePhase = "FailureCallback" /\
    gateOwner = "None" /\
    gateReleased /\
    backendResult = "Failed" /\
    failureSeen /\
    softwareCommitted /\
    ~configPublished /\
    "CONFIG" \notin events)

=============================================================================
