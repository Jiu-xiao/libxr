---------------------------- MODULE UartTxControl ----------------------------
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************
 * First composed protocol model for the UART TX control plane.
 *
 * This model intentionally describes protocol facts rather than copying the
 * C++ CAS guards.  It combines three writers, the coalescing service, the
 * hardware gate, deferred IRQ publication, CONFIG abort admission/close, and
 * old/new DMA generations.  Payload bytes and exact WritePort waiter states
 * are left for later refinements and the implementation-level checker.
****************************************************************************)

CONSTANTS
  MaxGeneration,
  DurableRetryObligation,
  EnableDeferredSelfDispatch,
  BindSubmitContext,
  StableConfigBoundary

Writers == {"A", "B", "C"}
NoContext == "NoContext"
NoRecord == "NoRecord"
NoGeneration == MaxGeneration + 1

ServiceContexts == Writers \cup {"IRQ", "DEFERRED", "CONFIG"}
ServiceEvents == {"WRITE", "COMPLETE", "ERROR", "CONFIG"}
GateActions == {"TX", "IRQ", "CONFIG"}
GateOwners == {"None", "TX", "IRQ", "CONFIG"}
WriterStates == {"Ready", "Queued", "Done", "Failed"}
IrqStates == {"Idle", "Masked", "Marked", "Dispatch", "Scan", "Leave"}
ConfigStates == {"Idle", "Prepare", "WaitQuiesce", "Close", "RetryClose"}
TerminalKinds == {"None", "Complete", "Error", "Both"}
GenerationDomain == 0..MaxGeneration
GenerationValues == 0..NoGeneration
NoBoundary == Cardinality(Writers) + 1

VARIABLES
  writerState,
  txQueue,
  events,
  serviceContext,
  serviceSnapshot,
  gateOwner,
  gatePending,
  txCandidate,
  txRetry,
  irqState,
  irqMasked,
  configState,
  configPublishCount,
  configPrefix,
  abortStateReady,
  abortAdmitted,
  abortActive,
  configQuiesced,
  dmaGeneration,
  retiredGeneration,
  activeRecord,
  activeGeneration,
  terminalKind,
  terminalGeneration

vars == <<
  writerState,
  txQueue,
  events,
  serviceContext,
  serviceSnapshot,
  gateOwner,
  gatePending,
  txCandidate,
  txRetry,
  irqState,
  irqMasked,
  configState,
  configPublishCount,
  configPrefix,
  abortStateReady,
  abortAdmitted,
  abortActive,
  configQuiesced,
  dmaGeneration,
  retiredGeneration,
  activeRecord,
  activeGeneration,
  terminalKind,
  terminalGeneration
>>

configBoundaryValid == configPrefix # NoBoundary

QueueRecords(q) == {q[i] : i \in 1..Len(q)}

PrefixRecords(q, count) ==
  IF count = 0
  THEN {}
  ELSE {q[i] : i \in 1..count}

DropPrefix(q, count) ==
  IF count = 0
  THEN q
  ELSE IF count >= Len(q)
       THEN <<>>
       ELSE SubSeq(q, count + 1, Len(q))

TerminalEvents(kind) ==
  CASE kind = "Complete" -> {"COMPLETE"}
    [] kind = "Error" -> {"ERROR"}
    [] kind = "Both" -> {"COMPLETE", "ERROR"}
    [] OTHER -> {}

RealConfig ==
  "CONFIG" \in serviceSnapshot /\ "CONFIG" \in gatePending

HasTerminalEvent ==
  "ERROR" \in serviceSnapshot \/ "COMPLETE" \in serviceSnapshot

Init ==
  /\ writerState = [w \in Writers |-> "Ready"]
  /\ txQueue = <<>>
  /\ events = {}
  /\ serviceContext = NoContext
  /\ serviceSnapshot = {}
  /\ gateOwner = "None"
  /\ gatePending = {}
  /\ txCandidate = NoRecord
  /\ txRetry = FALSE
  /\ irqState = "Idle"
  /\ irqMasked = FALSE
  /\ configState = "Idle"
  /\ configPublishCount = 0
  /\ configPrefix = NoBoundary
  /\ abortStateReady = FALSE
  /\ abortAdmitted = FALSE
  /\ abortActive = FALSE
  /\ configQuiesced = FALSE
  /\ dmaGeneration = 0
  /\ retiredGeneration = NoGeneration
  /\ activeRecord = NoRecord
  /\ activeGeneration = NoGeneration
  /\ terminalKind = "None"
  /\ terminalGeneration = NoGeneration

WriterSubmit(w) ==
  /\ writerState[w] = "Ready"
  /\ writerState' = [writerState EXCEPT ![w] = "Queued"]
  /\ txQueue' = Append(txQueue, w)
  /\ events' = events \cup {"WRITE"}
  /\ UNCHANGED <<serviceContext, serviceSnapshot, gateOwner, gatePending,
                  txCandidate, txRetry, irqState, irqMasked, configState,
                  configPrefix, abortStateReady, abortAdmitted, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  activeRecord, activeGeneration, terminalKind,
                  terminalGeneration>>

RequestConfig ==
  /\ configPublishCount < MaxGeneration
  /\ configPublishCount' = configPublishCount + 1
  /\ events' = events \cup {"CONFIG"}
  /\ gatePending' = gatePending \cup {"CONFIG"}
  /\ UNCHANGED <<writerState, txQueue, serviceContext, serviceSnapshot,
                  gateOwner, txCandidate, txRetry, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceClaim(ctx) ==
  /\ ctx \in ServiceContexts
  /\ ctx \in Writers => writerState[ctx] = "Queued"
  /\ serviceContext = NoContext
  /\ events # {}
  /\ serviceContext' = ctx
  /\ UNCHANGED <<writerState, txQueue, events, serviceSnapshot, gateOwner,
                  gatePending, txCandidate, txRetry, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceTakeSnapshot ==
  /\ serviceContext # NoContext
  /\ txCandidate = NoRecord
  /\ serviceSnapshot = {}
  /\ events # {}
  /\ serviceSnapshot' = events
  /\ events' = {}
  /\ UNCHANGED <<writerState, txQueue, serviceContext, gateOwner,
                  gatePending, txCandidate, txRetry, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceCaptureConfigBoundary ==
  /\ RealConfig
  /\ ~configBoundaryValid
  /\ configPrefix' = Len(txQueue)
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configState,
                  abortStateReady, abortAdmitted, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  activeRecord, activeGeneration, terminalKind,
                  terminalGeneration>>

ServiceBeginConfig ==
  /\ RealConfig
  /\ configBoundaryValid
  /\ gateOwner = "None"
  /\ configState = "Idle"
  /\ gateOwner' = "CONFIG"
  /\ gatePending' = gatePending \ {"CONFIG"}
  /\ configState' = "Prepare"
  /\ serviceSnapshot' = {}
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext, txCandidate,
                  txRetry, irqState, irqMasked, configPrefix, abortStateReady,
                  abortAdmitted, abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceDeferConfig ==
  /\ RealConfig
  /\ configBoundaryValid
  /\ (gateOwner # "None" \/ configState # "Idle")
  /\ serviceSnapshot' = {}
  /\ configPrefix' = IF StableConfigBoundary THEN configPrefix ELSE Len(txQueue)
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext, gateOwner,
                  gatePending, txCandidate, txRetry, irqState, irqMasked,
                  configState, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceHandleTerminal ==
  /\ serviceSnapshot # {}
  /\ ~RealConfig
  /\ ~configBoundaryValid
  /\ HasTerminalEvent
  /\ activeRecord # NoRecord
  /\ activeRecord' = NoRecord
  /\ activeGeneration' = NoGeneration
  /\ terminalGeneration' = NoGeneration
  /\ serviceSnapshot' = {}
  /\ events' = IF txQueue # <<>> THEN events \cup {"WRITE"} ELSE events
  /\ UNCHANGED <<writerState, txQueue, serviceContext, gateOwner,
                  gatePending, txCandidate, txRetry, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, terminalKind>>

ServiceIgnoreSpuriousTerminal ==
  /\ serviceSnapshot # {}
  /\ ~RealConfig
  /\ ~configBoundaryValid
  /\ HasTerminalEvent
  /\ activeRecord = NoRecord
  /\ serviceSnapshot' = {}
  /\ IF txCandidate = NoRecord /\ txQueue # <<>>
     THEN /\ txCandidate' = Head(txQueue)
          /\ gatePending' = gatePending \cup {"TX"}
          /\ txRetry' = TRUE
     ELSE /\ UNCHANGED txCandidate
          /\ UNCHANGED gatePending
          /\ UNCHANGED txRetry
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext, gateOwner,
                  irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceHandleWrite ==
  LET contextMismatch ==
        BindSubmitContext /\
        serviceContext \in Writers /\
        txQueue # <<>> /\
        serviceContext # Head(txQueue)
  IN
  /\ serviceSnapshot # {}
  /\ ~RealConfig
  /\ ~configBoundaryValid
  /\ ~HasTerminalEvent
  /\ "WRITE" \in serviceSnapshot
  /\ serviceSnapshot' = {}
  /\ IF ~contextMismatch /\ activeRecord = NoRecord /\
        txCandidate = NoRecord /\ txQueue # <<>>
     THEN /\ txCandidate' = Head(txQueue)
          /\ gatePending' = gatePending \cup {"TX"}
          /\ txRetry' = TRUE
     ELSE /\ UNCHANGED txCandidate
          /\ UNCHANGED gatePending
          /\ UNCHANGED txRetry
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext, gateOwner,
                  irqState, irqMasked, configState, configPrefix,
                  abortStateReady, abortAdmitted, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  activeRecord, activeGeneration, terminalKind,
                  terminalGeneration>>

ServiceDiscardSpuriousConfig ==
  /\ serviceSnapshot # {}
  /\ ~RealConfig
  /\ ~configBoundaryValid
  /\ ~HasTerminalEvent
  /\ "WRITE" \notin serviceSnapshot
  /\ serviceSnapshot' = {}
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext, gateOwner,
                  gatePending, txCandidate, txRetry, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceHoldForConfig ==
  /\ serviceSnapshot # {}
  /\ configBoundaryValid
  /\ ~RealConfig
  /\ serviceSnapshot' = {}
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext, gateOwner,
                  gatePending, txCandidate, txRetry, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ServiceRelease ==
  /\ serviceContext # NoContext
  /\ txCandidate = NoRecord
  /\ serviceSnapshot = {}
  /\ events = {}
  /\ serviceContext' = NoContext
  /\ UNCHANGED <<writerState, txQueue, events, serviceSnapshot, gateOwner,
                  gatePending, txCandidate, txRetry, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

TxGateClaim ==
  /\ txCandidate # NoRecord
  /\ gateOwner = "None"
  /\ "CONFIG" \notin gatePending
  /\ "IRQ" \notin gatePending
  /\ gateOwner' = "TX"
  /\ gatePending' = gatePending \ {"TX"}
  /\ txRetry' = FALSE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, txCandidate, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

TxGateBlocked ==
  /\ txCandidate # NoRecord
  /\ (gateOwner # "None" \/ "CONFIG" \in gatePending \/
      "IRQ" \in gatePending)
  /\ txCandidate' = NoRecord
  /\ gatePending' = IF DurableRetryObligation
                     THEN gatePending \cup {"TX"}
                     ELSE gatePending \ {"TX"}
  /\ txRetry' = DurableRetryObligation
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, irqState, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

TxStartStarted ==
  /\ gateOwner = "TX"
  /\ txCandidate # NoRecord
  /\ txQueue # <<>>
  /\ txCandidate = Head(txQueue)
  /\ activeRecord' = txCandidate
  /\ activeGeneration' = dmaGeneration
  /\ writerState' = [writerState EXCEPT ![txCandidate] = "Done"]
  /\ txQueue' = Tail(txQueue)
  /\ txCandidate' = NoRecord
  /\ gateOwner' = "None"
  /\ UNCHANGED <<events, serviceContext, serviceSnapshot, gatePending,
                  txRetry, irqState, irqMasked, configState, configPrefix,
                  abortStateReady, abortAdmitted, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  terminalKind, terminalGeneration>>

TxStartRetry ==
  /\ gateOwner = "TX"
  /\ txCandidate # NoRecord
  /\ gateOwner' = "None"
  /\ txCandidate' = NoRecord
  /\ gatePending' = IF DurableRetryObligation
                     THEN gatePending \cup {"TX"}
                     ELSE gatePending \ {"TX"}
  /\ txRetry' = DurableRetryObligation
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, irqState, irqMasked, configState,
                  configPrefix, abortStateReady, abortAdmitted, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  activeRecord, activeGeneration, terminalKind,
                  terminalGeneration>>

DispatchConfig ==
  /\ gateOwner = "None"
  /\ "CONFIG" \in gatePending
  /\ "CONFIG" \notin events
  /\ events' = events \cup {"CONFIG"}
  /\ UNCHANGED <<writerState, txQueue, serviceContext, serviceSnapshot,
                  gateOwner, gatePending, txCandidate, txRetry, irqState,
                  irqMasked, configState, configPrefix, abortStateReady,
                  abortAdmitted, abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

DispatchTx ==
  /\ gateOwner = "None"
  /\ "CONFIG" \notin gatePending
  /\ "IRQ" \notin gatePending
  /\ "TX" \in gatePending
  /\ txRetry
  /\ txCandidate = NoRecord
  /\ events' = events \cup {"WRITE"}
  /\ txRetry' = FALSE
  /\ UNCHANGED <<writerState, txQueue, serviceContext, serviceSnapshot,
                  gateOwner, gatePending, txCandidate, irqState,
                  irqMasked, configState, configPrefix, abortStateReady,
                  abortAdmitted, abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

DmaLatchTerminal(kind) ==
  /\ kind \in {"Complete", "Error", "Both"}
  /\ activeRecord # NoRecord
  /\ terminalKind = "None"
  /\ terminalGeneration = NoGeneration
  /\ terminalKind' = kind
  /\ terminalGeneration' = activeGeneration
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configState, configPrefix,
                  abortStateReady, abortAdmitted, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  activeRecord, activeGeneration>>

OrdinaryIrqClaim ==
  /\ irqState = "Idle"
  /\ terminalKind # "None"
  /\ gateOwner = "None"
  /\ "CONFIG" \notin gatePending
  /\ gateOwner' = "IRQ"
  /\ irqState' = "Scan"
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gatePending, txCandidate, txRetry,
                  irqMasked, configState, configPrefix, abortStateReady,
                  abortAdmitted, abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

IrqLoseAndMask ==
  /\ irqState = "Idle"
  /\ terminalKind # "None"
  /\ (gateOwner # "None" \/ "CONFIG" \in gatePending)
  /\ irqState' = "Masked"
  /\ irqMasked' = TRUE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, configState, configPrefix, abortStateReady,
                  abortAdmitted, abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

IrqMarkDeferred ==
  /\ irqState = "Masked"
  /\ irqState' = IF EnableDeferredSelfDispatch THEN "Dispatch" ELSE "Marked"
  /\ gatePending' = gatePending \cup {"IRQ"}
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, txCandidate, txRetry,
                  irqMasked, configState, configPrefix, abortStateReady,
                  abortAdmitted, abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

IrqSelfDispatchClaim ==
  /\ irqState = "Dispatch"
  /\ gateOwner = "None"
  /\ "CONFIG" \notin gatePending
  /\ "IRQ" \in gatePending
  /\ gateOwner' = "IRQ"
  /\ gatePending' = gatePending \ {"IRQ"}
  /\ irqState' = "Scan"
  /\ irqMasked' = TRUE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, txCandidate, txRetry, configState,
                  configPrefix, abortStateReady, abortAdmitted, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  activeRecord, activeGeneration, terminalKind,
                  terminalGeneration>>

IrqScan ==
  /\ gateOwner = "IRQ"
  /\ irqState = "Scan"
  /\ events' = events \cup TerminalEvents(terminalKind)
  /\ terminalKind' = "None"
  /\ irqState' = "Leave"
  /\ UNCHANGED <<writerState, txQueue, serviceContext, serviceSnapshot,
                  gateOwner, gatePending, txCandidate, txRetry, irqMasked,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalGeneration>>

IrqLeave ==
  /\ gateOwner = "IRQ"
  /\ irqState = "Leave"
  /\ gateOwner' = "None"
  /\ irqState' = "Idle"
  /\ irqMasked' = FALSE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gatePending, txCandidate, txRetry,
                  configState, configPrefix, abortStateReady, abortAdmitted,
                  abortActive, configQuiesced, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ConfigOpenAbortAdmission ==
  /\ gateOwner = "CONFIG"
  /\ configState = "Prepare"
  /\ abortStateReady' = TRUE
  /\ abortAdmitted' = TRUE
  /\ configState' = "WaitQuiesce"
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configPrefix, abortActive,
                  configQuiesced, dmaGeneration, retiredGeneration,
                  activeRecord, activeGeneration, terminalKind,
                  terminalGeneration>>

AbortEnter ==
  /\ gateOwner = "CONFIG"
  /\ abortAdmitted
  /\ abortStateReady
  /\ ~abortActive
  /\ abortActive' = TRUE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configState, configPrefix,
                  abortStateReady, abortAdmitted, configQuiesced,
                  dmaGeneration, retiredGeneration, activeRecord,
                  activeGeneration, terminalKind, terminalGeneration>>

AbortPublishQuiescence ==
  /\ gateOwner = "CONFIG"
  /\ configState = "WaitQuiesce"
  /\ abortActive
  /\ ~configQuiesced
  /\ configQuiesced' = TRUE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configState, configPrefix,
                  abortStateReady, abortAdmitted, abortActive, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

AbortLeave ==
  /\ abortActive
  /\ abortActive' = FALSE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configState, configPrefix,
                  abortStateReady, abortAdmitted, configQuiesced,
                  dmaGeneration, retiredGeneration, activeRecord,
                  activeGeneration, terminalKind, terminalGeneration>>

ConfigQuiesceSynchronously ==
  /\ gateOwner = "CONFIG"
  /\ configState = "WaitQuiesce"
  /\ ~abortActive
  /\ ~configQuiesced
  /\ configQuiesced' = TRUE
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configState, configPrefix,
                  abortStateReady, abortAdmitted, abortActive, dmaGeneration,
                  retiredGeneration, activeRecord, activeGeneration,
                  terminalKind, terminalGeneration>>

ConfigApply ==
  LET oldRecords == PrefixRecords(txQueue, configPrefix) IN
  /\ gateOwner = "CONFIG"
  /\ configState = "WaitQuiesce"
  /\ configQuiesced
  /\ dmaGeneration < MaxGeneration
  /\ writerState' = [w \in Writers |->
       IF w \in oldRecords /\ writerState[w] # "Done"
       THEN "Failed"
       ELSE writerState[w]]
  /\ txQueue' = DropPrefix(txQueue, configPrefix)
  /\ txCandidate' = NoRecord
  /\ activeRecord' = NoRecord
  /\ activeGeneration' = NoGeneration
  /\ terminalKind' = "None"
  /\ terminalGeneration' = NoGeneration
  /\ retiredGeneration' = dmaGeneration
  /\ dmaGeneration' = dmaGeneration + 1
  /\ configPrefix' = NoBoundary
  /\ configState' = "Close"
  /\ UNCHANGED <<events, serviceContext, serviceSnapshot, gateOwner,
                  gatePending, txRetry, irqState, irqMasked, abortStateReady,
                  abortAdmitted, abortActive, configQuiesced>>

ConfigCloseDirect ==
  /\ gateOwner = "CONFIG"
  /\ configState = "Close"
  /\ ~abortActive
  /\ gateOwner' = "None"
  /\ configState' = "Idle"
  /\ abortStateReady' = FALSE
  /\ abortAdmitted' = FALSE
  /\ configQuiesced' = FALSE
  /\ events' = IF txQueue # <<>> THEN events \cup {"WRITE"} ELSE events
  /\ UNCHANGED <<writerState, txQueue, serviceContext,
                  serviceSnapshot, gatePending, txCandidate, txRetry,
                  irqState, irqMasked, configPrefix, abortActive,
                  dmaGeneration, retiredGeneration, activeRecord,
                  activeGeneration, terminalKind, terminalGeneration>>

ConfigCloseBlocked ==
  /\ gateOwner = "CONFIG"
  /\ configState = "Close"
  /\ abortActive
  /\ abortAdmitted' = FALSE
  /\ configState' = "RetryClose"
  /\ UNCHANGED <<writerState, txQueue, events, serviceContext,
                  serviceSnapshot, gateOwner, gatePending, txCandidate,
                  txRetry, irqState, irqMasked, configPrefix,
                  abortStateReady, abortActive, configQuiesced,
                  dmaGeneration, retiredGeneration, activeRecord,
                  activeGeneration, terminalKind, terminalGeneration>>

ConfigRetryClose ==
  /\ gateOwner = "CONFIG"
  /\ configState = "RetryClose"
  /\ ~abortActive
  /\ gateOwner' = "None"
  /\ configState' = "Idle"
  /\ abortStateReady' = FALSE
  /\ abortAdmitted' = FALSE
  /\ configQuiesced' = FALSE
  /\ events' = IF txQueue # <<>> THEN events \cup {"WRITE"} ELSE events
  /\ UNCHANGED <<writerState, txQueue, serviceContext,
                  serviceSnapshot, gatePending, txCandidate, txRetry,
                  irqState, irqMasked, configPrefix, abortActive,
                  dmaGeneration, retiredGeneration, activeRecord,
                  activeGeneration, terminalKind, terminalGeneration>>

KeepConfigBudget(action) ==
  /\ action
  /\ UNCHANGED configPublishCount

ProtocolNext ==
  \/ \E w \in Writers : WriterSubmit(w)
  \/ \E ctx \in ServiceContexts : ServiceClaim(ctx)
  \/ ServiceTakeSnapshot
  \/ ServiceCaptureConfigBoundary
  \/ ServiceBeginConfig
  \/ ServiceDeferConfig
  \/ ServiceHandleTerminal
  \/ ServiceIgnoreSpuriousTerminal
  \/ ServiceHandleWrite
  \/ ServiceDiscardSpuriousConfig
  \/ ServiceHoldForConfig
  \/ ServiceRelease
  \/ TxGateClaim
  \/ TxGateBlocked
  \/ TxStartStarted
  \/ TxStartRetry
  \/ DispatchConfig
  \/ DispatchTx
  \/ \E kind \in {"Complete", "Error", "Both"} : DmaLatchTerminal(kind)
  \/ OrdinaryIrqClaim
  \/ IrqLoseAndMask
  \/ IrqMarkDeferred
  \/ IrqSelfDispatchClaim
  \/ IrqScan
  \/ IrqLeave
  \/ ConfigOpenAbortAdmission
  \/ AbortEnter
  \/ AbortPublishQuiescence
  \/ AbortLeave
  \/ ConfigQuiesceSynchronously
  \/ ConfigApply
  \/ ConfigCloseDirect
  \/ ConfigCloseBlocked
  \/ ConfigRetryClose

Next == RequestConfig \/ KeepConfigBudget(ProtocolNext)

Spec == Init /\ [][Next]_vars

(***************************************************************************
 * Focused CONFIG progress specification.  RequestConfig is a finite
 * environment actor: weak fairness forces all bounded publications to occur,
 * while every internal continuation is weakly fair.  Multiple publications
 * may still coalesce into one level-triggered CONFIG transaction.
 *************************************************************************)

ConfigServiceClaim == \E ctx \in ServiceContexts : ServiceClaim(ctx)

ConfigCloseStep == ConfigCloseDirect \/ ConfigCloseBlocked

ConfigQuiesceStep ==
  AbortPublishQuiescence \/ ConfigQuiesceSynchronously

ConfigProgressAction ==
  \/ ConfigServiceClaim
  \/ ServiceTakeSnapshot
  \/ ServiceCaptureConfigBoundary
  \/ ServiceBeginConfig
  \/ ServiceDeferConfig
  \/ ServiceDiscardSpuriousConfig
  \/ ServiceHoldForConfig
  \/ ServiceRelease
  \/ DispatchConfig
  \/ ConfigOpenAbortAdmission
  \/ AbortEnter
  \/ AbortPublishQuiescence
  \/ AbortLeave
  \/ ConfigQuiesceSynchronously
  \/ ConfigApply
  \/ ConfigCloseStep
  \/ ConfigRetryClose

ConfigProgressNext ==
  RequestConfig \/ KeepConfigBudget(ConfigProgressAction)

ConfigProgressFairness ==
  /\ WF_vars(RequestConfig)
  /\ WF_vars(KeepConfigBudget(ConfigServiceClaim))
  /\ WF_vars(KeepConfigBudget(ServiceTakeSnapshot))
  /\ WF_vars(KeepConfigBudget(ServiceCaptureConfigBoundary))
  /\ WF_vars(KeepConfigBudget(ServiceBeginConfig))
  /\ WF_vars(KeepConfigBudget(ServiceDeferConfig))
  /\ WF_vars(KeepConfigBudget(ServiceDiscardSpuriousConfig))
  /\ WF_vars(KeepConfigBudget(ServiceHoldForConfig))
  /\ WF_vars(KeepConfigBudget(ServiceRelease))
  /\ WF_vars(KeepConfigBudget(DispatchConfig))
  /\ WF_vars(KeepConfigBudget(ConfigOpenAbortAdmission))
  /\ WF_vars(KeepConfigBudget(AbortEnter))
  /\ WF_vars(KeepConfigBudget(AbortLeave))
  /\ WF_vars(KeepConfigBudget(ConfigQuiesceStep))
  /\ WF_vars(KeepConfigBudget(ConfigApply))
  /\ WF_vars(KeepConfigBudget(ConfigCloseStep))
  /\ WF_vars(KeepConfigBudget(ConfigRetryClose))

ConfigProgressSpec ==
  Init /\ [][ConfigProgressNext]_vars /\ ConfigProgressFairness

(***************************************************************************
 * Initial safety properties.  Later revisions should add temporal progress
 * properties and a refinement map to the implementation-level checker.
****************************************************************************)

TypeOK ==
  /\ MaxGeneration \in Nat
  /\ MaxGeneration > 0
  /\ DurableRetryObligation \in BOOLEAN
  /\ EnableDeferredSelfDispatch \in BOOLEAN
  /\ BindSubmitContext \in BOOLEAN
  /\ StableConfigBoundary \in BOOLEAN
  /\ writerState \in [Writers -> WriterStates]
  /\ txQueue \in Seq(Writers)
  /\ events \subseteq ServiceEvents
  /\ serviceContext \in ServiceContexts \cup {NoContext}
  /\ serviceSnapshot \subseteq ServiceEvents
  /\ gateOwner \in GateOwners
  /\ gatePending \subseteq GateActions
  /\ txCandidate \in Writers \cup {NoRecord}
  /\ txRetry \in BOOLEAN
  /\ irqState \in IrqStates
  /\ irqMasked \in BOOLEAN
  /\ configState \in ConfigStates
  /\ configPublishCount \in 0..MaxGeneration
  /\ configPrefix \in 0..NoBoundary
  /\ configBoundaryValid => configPrefix <= Len(txQueue)
  /\ abortStateReady \in BOOLEAN
  /\ abortAdmitted \in BOOLEAN
  /\ abortActive \in BOOLEAN
  /\ configQuiesced \in BOOLEAN
  /\ dmaGeneration \in GenerationDomain
  /\ retiredGeneration \in GenerationValues
  /\ activeRecord \in Writers \cup {NoRecord}
  /\ activeGeneration \in GenerationValues
  /\ terminalKind \in TerminalKinds
  /\ terminalGeneration \in GenerationValues

QueueHasNoDuplicates ==
  Cardinality(QueueRecords(txQueue)) = Len(txQueue)

QueueStateConsistent ==
  \A w \in Writers :
    (writerState[w] = "Queued") <=> (w \in QueueRecords(txQueue))

ActiveGenerationConsistent ==
  (activeRecord = NoRecord) <=> (activeGeneration = NoGeneration)

TerminalGenerationConsistent ==
  terminalKind # "None" => terminalGeneration # NoGeneration

NoStaleLatchedTerminal ==
  terminalGeneration = NoGeneration \/
    (activeRecord # NoRecord /\ terminalGeneration = activeGeneration)

ConfigOwnsGateUntilClosed ==
  (configState # "Idle") <=> (gateOwner = "CONFIG")

AbortAdmissionIsScoped ==
  (abortAdmitted \/ abortActive) => gateOwner = "CONFIG"

RetryObligationIsDurable ==
  txRetry => ("TX" \in gatePending)

CandidateIsQueueHead ==
  txCandidate = NoRecord \/
    (txQueue # <<>> /\ txCandidate = Head(txQueue))

(***************************************************************************
 * Sensitivity properties for intentionally broken configurations.
 *
 * A queued record needs at least one carrier that can cause another service
 * step.  An active hardware/config owner is also a carrier because its leave
 * path will expose pending actions.  Clearing an ephemeral RETRY hint before
 * dispatch removes the final carrier and violates this invariant.
****************************************************************************)
TxWorkHasCarrier ==
  \/ txQueue = <<>>
  \/ activeRecord # NoRecord
  \/ txCandidate # NoRecord
  \/ "WRITE" \in events
  \/ "WRITE" \in serviceSnapshot
  \/ txRetry
  \/ "CONFIG" \in events
  \/ "CONFIG" \in serviceSnapshot
  \/ "CONFIG" \in gatePending
  \/ configState # "Idle"
  \/ gateOwner # "None"

(***************************************************************************
 * If the owner releases between a losing IRQ's mask and mark operations, the
 * publisher itself is the only actor guaranteed to notice the newly durable
 * IRQ action.  Disabling self-dispatch therefore leaves Marked work stranded
 * once no owner remains.
****************************************************************************)
DeferredWorkHasDispatcher ==
  irqState # "Marked" \/
    gateOwner # "None"

ConfigBoundaryHasCarrier ==
  ~configBoundaryValid \/
    "CONFIG" \in gatePending \/
    "CONFIG" \in events \/
    "CONFIG" \in serviceSnapshot \/
    configState # "Idle"

ConfigBoundaryStableStep ==
  (configBoundaryValid /\ configPrefix' # NoBoundary) =>
    configPrefix' = configPrefix

ConfigBoundaryDoesNotMove ==
  [][ConfigBoundaryStableStep]_vars

ConcurrentSubmitPreservesOwnerStep ==
  \A w \in Writers :
    (writerState[w] = "Ready" /\ writerState'[w] = "Queued") =>
      serviceContext' = serviceContext

ConcurrentSubmitPreservesOwner ==
  [][ConcurrentSubmitPreservesOwnerStep]_vars

ReleaseOnlyWhenEmptyStep ==
  (serviceContext # NoContext /\ serviceContext' = NoContext) =>
    (events = {} /\ serviceSnapshot = {} /\ txCandidate = NoRecord)

ReleaseOnlyWhenEmpty ==
  [][ReleaseOnlyWhenEmptyStep]_vars

(***************************************************************************
 * Reachability witnesses.  Their dedicated negative configurations assert
 * these predicates as invariants and must fail.  This makes the runner prove
 * that writer publication is actually interleaved with an existing owner,
 * rather than merely being present as a syntactically enabled action.
****************************************************************************)
NoSubmitAfterTakeWitness ==
  ~(serviceContext # NoContext /\
    serviceSnapshot = {"WRITE"} /\
    "WRITE" \in events /\
    Len(txQueue) >= 2 /\
    activeRecord = NoRecord /\
    txCandidate = NoRecord)

NoOwnerRetakeWitness ==
  ~(activeRecord # NoRecord /\
    serviceContext = activeRecord /\
    serviceSnapshot = {"WRITE"} /\
    events = {} /\
    txQueue # <<>> /\
     txCandidate = NoRecord)

ConfigObligation ==
  \/ "CONFIG" \in events
  \/ "CONFIG" \in serviceSnapshot
  \/ "CONFIG" \in gatePending
  \/ configBoundaryValid
  \/ configState # "Idle"

ConfigPublisherExhaustsBudget ==
  configPublishCount < MaxGeneration ~> configPublishCount = MaxGeneration

EveryConfigObligationSettles == ConfigObligation ~> ~ConfigObligation

=============================================================================
