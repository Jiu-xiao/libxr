------------------------- MODULE UartHardwareControl -------------------------
EXTENDS Naturals, FiniteSets, TLC

(***************************************************************************
 * Bounded UART hardware-control protocol.
 *
 * The two IRQ publishers execute on one logical IRQ core.  Their local PCs
 * are therefore ordered, but a scanner or CONFIG owner on another core may
 * interleave between mask, mark, and self-dispatch.  CONFIG transactions and
 * the delayed abort callback carry ghost generations so that late DMA status
 * and admission ABA are observable properties.  A correct backend does not
 * need a runtime epoch: it must not report CONFIG quiesced while an already
 * selected abort callback can still enter later.
 *************************************************************************)

CONSTANTS ScannerRemasks,
          AllowLateOldStatus,
          AllowEarlyConfigQuiesce,
          AllowEarlyConfigClose

ASSUME /\ ScannerRemasks \in BOOLEAN
       /\ AllowLateOldStatus \in BOOLEAN
       /\ AllowEarlyConfigQuiesce \in BOOLEAN
       /\ AllowEarlyConfigClose \in BOOLEAN

IrqActors == {"IRQ0", "IRQ1"}
NoIrq == "NoIrq"
IrqPcs == {"Ready", "Masked", "Marked", "Done"}

ScannerPcs == {"Idle", "Claimed", "ReadyToScan", "Scanned"}
ConfigPcs == {"Idle", "Quiesce", "Apply", "Close"}
AbortPcs == {"Ready", "Captured", "Active", "Done"}

MaxGeneration == 2
Generations == 0..MaxGeneration
NoGeneration == MaxGeneration + 1
GenerationValues == Generations \cup {NoGeneration}

VARIABLES irqPc,
          irqCoreActive,
          irqMasked,
          deferredPending,
          dispatchRequested,
          scannerPc,
          scannerOwner,
          scanCount,
          lastScanMasked,
          configPc,
          configOwner,
          configQuiesced,
          completedConfigs,
          admissionGeneration,
          admissionOpen,
          abortStateReady,
          abortPc,
          abortCapturedGeneration,
          abortEnteredGeneration,
          dmaGeneration,
          activeGeneration,
          retiredGeneration,
          statusPending,
          statusGeneration

vars == <<irqPc, irqCoreActive, irqMasked, deferredPending,
          dispatchRequested, scannerPc, scannerOwner, scanCount,
          lastScanMasked, configPc, configOwner, configQuiesced,
          completedConfigs, admissionGeneration, admissionOpen,
          abortStateReady, abortPc, abortCapturedGeneration,
          abortEnteredGeneration, dmaGeneration, activeGeneration,
          retiredGeneration, statusPending, statusGeneration>>

Init ==
  /\ irqPc = [i \in IrqActors |-> "Ready"]
  /\ irqCoreActive = NoIrq
  /\ irqMasked = FALSE
  /\ deferredPending = FALSE
  /\ dispatchRequested = FALSE
  /\ scannerPc = "Idle"
  /\ scannerOwner = FALSE
  /\ scanCount = 0
  /\ lastScanMasked = TRUE
  /\ configPc = "Idle"
  /\ configOwner = FALSE
  /\ configQuiesced = FALSE
  /\ completedConfigs = 0
  /\ admissionGeneration = 0
  /\ admissionOpen = FALSE
  /\ abortStateReady = FALSE
  /\ abortPc = "Ready"
  /\ abortCapturedGeneration = NoGeneration
  /\ abortEnteredGeneration = NoGeneration
  /\ dmaGeneration = 0
  /\ activeGeneration = 0
  /\ retiredGeneration = NoGeneration
  /\ statusPending = FALSE
  /\ statusGeneration = NoGeneration

CanStartIrq(i) ==
  /\ irqCoreActive = NoIrq
  /\ irqPc[i] = "Ready"
  /\ (i = "IRQ0" \/ irqPc["IRQ0"] = "Done")
  /\ (configOwner \/ scannerOwner)

IrqMask(i) ==
  /\ i \in IrqActors
  /\ CanStartIrq(i)
  /\ irqPc' = [irqPc EXCEPT ![i] = "Masked"]
  /\ irqCoreActive' = i
  /\ irqMasked' = TRUE
  /\ UNCHANGED <<deferredPending, dispatchRequested, scannerPc,
                  scannerOwner, scanCount, lastScanMasked, configPc,
                  configOwner, configQuiesced, completedConfigs,
                  admissionGeneration, admissionOpen, abortStateReady,
                  abortPc, abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration, activeGeneration, retiredGeneration,
                  statusPending, statusGeneration>>

IrqMark(i) ==
  /\ i \in IrqActors
  /\ irqCoreActive = i
  /\ irqPc[i] = "Masked"
  /\ irqPc' = [irqPc EXCEPT ![i] = "Marked"]
  /\ deferredPending' = TRUE
  /\ UNCHANGED <<irqCoreActive, irqMasked, dispatchRequested, scannerPc,
                  scannerOwner, scanCount, lastScanMasked, configPc,
                  configOwner, configQuiesced, completedConfigs,
                  admissionGeneration, admissionOpen, abortStateReady,
                  abortPc, abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration, activeGeneration, retiredGeneration,
                  statusPending, statusGeneration>>

IrqSelfDispatch(i) ==
  /\ i \in IrqActors
  /\ irqCoreActive = i
  /\ irqPc[i] = "Marked"
  /\ irqPc' = [irqPc EXCEPT ![i] = "Done"]
  /\ irqCoreActive' = NoIrq
  /\ dispatchRequested' = TRUE
  /\ UNCHANGED <<irqMasked, deferredPending, scannerPc, scannerOwner,
                  scanCount, lastScanMasked, configPc, configOwner,
                  configQuiesced, completedConfigs, admissionGeneration,
                  admissionOpen, abortStateReady, abortPc,
                  abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration, activeGeneration, retiredGeneration,
                  statusPending, statusGeneration>>

ScannerClaim ==
  /\ scannerPc = "Idle"
  /\ ~scannerOwner
  /\ ~configOwner
  /\ deferredPending
  /\ dispatchRequested
  /\ scannerPc' = "Claimed"
  /\ scannerOwner' = TRUE
  /\ deferredPending' = FALSE
  /\ dispatchRequested' = FALSE
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortPc, abortCapturedGeneration,
                  abortEnteredGeneration, dmaGeneration, activeGeneration,
                  retiredGeneration, statusPending, statusGeneration>>

ScannerPrepareScan ==
  /\ scannerPc = "Claimed"
  /\ scannerOwner
  /\ scannerPc' = "ReadyToScan"
  /\ irqMasked' = IF ScannerRemasks THEN TRUE ELSE irqMasked
  /\ UNCHANGED <<irqPc, irqCoreActive, deferredPending,
                  dispatchRequested, scannerOwner, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortPc, abortCapturedGeneration,
                  abortEnteredGeneration, dmaGeneration, activeGeneration,
                  retiredGeneration, statusPending, statusGeneration>>

ScannerScan ==
  /\ scannerPc = "ReadyToScan"
  /\ scannerOwner
  /\ scannerPc' = "Scanned"
  /\ scanCount' = scanCount + 1
  /\ lastScanMasked' = irqMasked
  /\ statusPending' = FALSE
  /\ statusGeneration' = NoGeneration
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerOwner, configPc, configOwner,
                  configQuiesced, completedConfigs, admissionGeneration,
                  admissionOpen, abortStateReady, abortPc,
                  abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration, activeGeneration, retiredGeneration>>

ScannerRestore ==
  /\ scannerPc = "Scanned"
  /\ scannerOwner
  /\ scannerPc' = "Idle"
  /\ scannerOwner' = FALSE
  /\ irqMasked' = FALSE
  /\ UNCHANGED <<irqPc, irqCoreActive, deferredPending,
                  dispatchRequested, scanCount, lastScanMasked, configPc,
                  configOwner, configQuiesced, completedConfigs,
                  admissionGeneration, admissionOpen, abortStateReady,
                  abortPc, abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration, activeGeneration, retiredGeneration,
                  statusPending, statusGeneration>>

ConfigBegin ==
  /\ configPc = "Idle"
  /\ ~configOwner
  /\ ~scannerOwner
  /\ completedConfigs < MaxGeneration
  /\ configPc' = "Quiesce"
  /\ configOwner' = TRUE
  /\ configQuiesced' = FALSE
  /\ admissionGeneration' = completedConfigs + 1
  /\ admissionOpen' = TRUE
  /\ abortStateReady' = TRUE
  /\ irqMasked' = TRUE
  /\ UNCHANGED <<irqPc, irqCoreActive, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, completedConfigs, abortPc,
                  abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration, activeGeneration, retiredGeneration,
                  statusPending, statusGeneration>>

ConfigQuiesce ==
  /\ configPc = "Quiesce"
  /\ configOwner
  /\ (AllowEarlyConfigQuiesce \/ abortPc \in {"Ready", "Done"})
  /\ configPc' = "Apply"
  /\ configQuiesced' = TRUE
  /\ retiredGeneration' = activeGeneration
  /\ activeGeneration' = NoGeneration
  /\ statusPending' = FALSE
  /\ statusGeneration' = NoGeneration
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configOwner, completedConfigs,
                  admissionGeneration, admissionOpen, abortStateReady,
                  abortPc, abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration>>

ConfigApply ==
  /\ configPc = "Apply"
  /\ configOwner
  /\ configQuiesced
  /\ abortPc # "Active"
  /\ (AllowEarlyConfigClose \/ abortPc # "Captured")
  /\ dmaGeneration < MaxGeneration
  /\ configPc' = "Close"
  /\ dmaGeneration' = dmaGeneration + 1
  /\ activeGeneration' = dmaGeneration + 1
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortPc, abortCapturedGeneration,
                  abortEnteredGeneration, retiredGeneration, statusPending,
                  statusGeneration>>

ConfigClose ==
  /\ configPc = "Close"
  /\ configOwner
  /\ abortPc # "Active"
  /\ (AllowEarlyConfigClose \/ abortPc # "Captured")
  /\ configPc' = "Idle"
  /\ configOwner' = FALSE
  /\ configQuiesced' = FALSE
  /\ completedConfigs' = admissionGeneration
  /\ admissionOpen' = FALSE
  /\ abortStateReady' = FALSE
  /\ irqMasked' = FALSE
  /\ UNCHANGED <<irqPc, irqCoreActive, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, admissionGeneration, abortPc,
                  abortCapturedGeneration, abortEnteredGeneration,
                  dmaGeneration, activeGeneration, retiredGeneration,
                  statusPending, statusGeneration>>

AbortCapture ==
  /\ abortPc = "Ready"
  /\ configOwner
  /\ admissionOpen
  /\ abortStateReady
  /\ ~configQuiesced
  /\ admissionGeneration = 1
  /\ abortPc' = "Captured"
  /\ abortCapturedGeneration' = admissionGeneration
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortEnteredGeneration, dmaGeneration,
                  activeGeneration, retiredGeneration, statusPending,
                  statusGeneration>>

AbortEnter ==
  /\ abortPc = "Captured"
  /\ configOwner
  /\ admissionOpen
  /\ abortStateReady
  /\ abortPc' = "Active"
  /\ abortEnteredGeneration' = admissionGeneration
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortCapturedGeneration, dmaGeneration,
                  activeGeneration, retiredGeneration, statusPending,
                  statusGeneration>>

AbortRejectClosed ==
  /\ abortPc = "Captured"
  /\ (\/ ~admissionOpen
      \/ ~configOwner)
  /\ abortPc' = "Done"
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortCapturedGeneration,
                  abortEnteredGeneration, dmaGeneration, activeGeneration,
                  retiredGeneration, statusPending, statusGeneration>>

AbortLeave ==
  /\ abortPc = "Active"
  /\ abortPc' = "Done"
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortCapturedGeneration,
                  abortEnteredGeneration, dmaGeneration, activeGeneration,
                  retiredGeneration, statusPending, statusGeneration>>

DmaLatchCurrentStatus ==
  /\ activeGeneration # NoGeneration
  /\ ~statusPending
  /\ statusPending' = TRUE
  /\ statusGeneration' = activeGeneration
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortPc, abortCapturedGeneration,
                  abortEnteredGeneration, dmaGeneration, activeGeneration,
                  retiredGeneration>>

DmaLatchLateOldStatus ==
  /\ AllowLateOldStatus
  /\ activeGeneration # NoGeneration
  /\ retiredGeneration # NoGeneration
  /\ retiredGeneration # activeGeneration
  /\ ~statusPending
  /\ statusPending' = TRUE
  /\ statusGeneration' = retiredGeneration
  /\ UNCHANGED <<irqPc, irqCoreActive, irqMasked, deferredPending,
                  dispatchRequested, scannerPc, scannerOwner, scanCount,
                  lastScanMasked, configPc, configOwner, configQuiesced,
                  completedConfigs, admissionGeneration, admissionOpen,
                  abortStateReady, abortPc, abortCapturedGeneration,
                  abortEnteredGeneration, dmaGeneration, activeGeneration,
                  retiredGeneration>>

Next ==
  \/ \E i \in IrqActors : IrqMask(i)
  \/ \E i \in IrqActors : IrqMark(i)
  \/ \E i \in IrqActors : IrqSelfDispatch(i)
  \/ ScannerClaim
  \/ ScannerPrepareScan
  \/ ScannerScan
  \/ ScannerRestore
  \/ ConfigBegin
  \/ ConfigQuiesce
  \/ ConfigApply
  \/ ConfigClose
  \/ AbortCapture
  \/ AbortEnter
  \/ AbortRejectClosed
  \/ AbortLeave
  \/ DmaLatchCurrentStatus
  \/ DmaLatchLateOldStatus

Spec == Init /\ [][Next]_vars

FairSpec ==
  /\ Spec
  /\ \A i \in IrqActors :
       /\ WF_vars(IrqMark(i))
       /\ WF_vars(IrqSelfDispatch(i))
  /\ WF_vars(ScannerClaim)
  /\ WF_vars(ScannerPrepareScan)
  /\ WF_vars(ScannerScan)
  /\ WF_vars(ScannerRestore)
  /\ WF_vars(ConfigQuiesce)
  /\ WF_vars(ConfigApply)
  /\ WF_vars(ConfigClose)
  /\ WF_vars(AbortEnter)
  /\ WF_vars(AbortRejectClosed)
  /\ WF_vars(AbortLeave)

TypeOK ==
  /\ irqPc \in [IrqActors -> IrqPcs]
  /\ irqCoreActive \in IrqActors \cup {NoIrq}
  /\ irqMasked \in BOOLEAN
  /\ deferredPending \in BOOLEAN
  /\ dispatchRequested \in BOOLEAN
  /\ scannerPc \in ScannerPcs
  /\ scannerOwner \in BOOLEAN
  /\ scanCount \in Nat
  /\ lastScanMasked \in BOOLEAN
  /\ configPc \in ConfigPcs
  /\ configOwner \in BOOLEAN
  /\ configQuiesced \in BOOLEAN
  /\ completedConfigs \in Generations
  /\ admissionGeneration \in Generations
  /\ admissionOpen \in BOOLEAN
  /\ abortStateReady \in BOOLEAN
  /\ abortPc \in AbortPcs
  /\ abortCapturedGeneration \in GenerationValues
  /\ abortEnteredGeneration \in GenerationValues
  /\ dmaGeneration \in Generations
  /\ activeGeneration \in GenerationValues
  /\ retiredGeneration \in GenerationValues
  /\ statusPending \in BOOLEAN
  /\ statusGeneration \in GenerationValues

OwnersUnique == ~(scannerOwner /\ configOwner)

SameCoreIrqsAreSequential ==
  /\ (irqPc["IRQ1"] # "Ready" => irqPc["IRQ0"] = "Done")
  /\ \A i \in IrqActors :
       (irqPc[i] \in {"Masked", "Marked"}) <=> (irqCoreActive = i)

ScanRequiresMask == scanCount = 0 \/ lastScanMasked

StatusMatchesActiveGeneration ==
  /\ (statusPending =>
        activeGeneration # NoGeneration /\
        statusGeneration = activeGeneration)
  /\ (~statusPending => statusGeneration = NoGeneration)

OldAbortCannotEnterNewAdmission ==
  abortPc # "Active" \/
    (abortCapturedGeneration = admissionGeneration /\
     abortEnteredGeneration = admissionGeneration)

AdmissionIsInitializedAndScoped ==
  /\ (admissionOpen => configOwner /\ abortStateReady)
  /\ (abortPc = "Active" => configOwner /\ admissionOpen)

QuiescenceRequiresSettledAbort ==
  configQuiesced => abortPc \in {"Ready", "Done"}

ConfigProgressRequiresSettledAbortStep ==
  ((configPc = "Apply" /\ configPc' = "Close") \/
   (configPc = "Close" /\ configPc' = "Idle")) =>
    abortPc \in {"Ready", "Done"}

ConfigProgressRequiresSettledAbort ==
  [][ConfigProgressRequiresSettledAbortStep]_vars

StartedIrqPublisherFinishes ==
  \A i \in IrqActors : irqPc[i] \in {"Masked", "Marked"} ~> irqPc[i] = "Done"

DeferredEventuallyClaimed == deferredPending ~> ~deferredPending

ScannerEventuallyReleases == scannerOwner ~> ~scannerOwner

ConfigEventuallyCloses == configOwner ~> ~configOwner

AbortEventuallySettles ==
  /\ abortPc = "Captured" ~> abortPc \in {"Active", "Done"}
  /\ abortPc = "Active" ~> abortPc = "Done"

=============================================================================
