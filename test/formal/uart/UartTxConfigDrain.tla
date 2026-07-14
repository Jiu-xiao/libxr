------------------------- MODULE UartTxConfigDrain -------------------------
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************
 * Bounded CONFIG-drain model for four TX records.
 *
 * The initial pipeline contains:
 * - an active record whose operation already finished OK;
 * - a pending record at the metadata head whose payload was already removed;
 * - one ordinary queued record with both metadata and payload present;
 * - one fresh record published payload -> metadata -> WRITE from the pending
 *   record's Finish callback, after CONFIG captured its fixed prefix.
 *
 * MovingBoundary and PendingPopsPayload are independent single-fault switches.
 *************************************************************************)

CONSTANTS Records, MovingBoundary, PendingPopsPayload

ASSUME /\ Cardinality(Records) = 4
       /\ MovingBoundary \in BOOLEAN
       /\ PendingPopsPayload \in BOOLEAN

NoRecord == "NoRecord"
NoBoundary == Cardinality(Records) + 1

OperationStates == {"Fresh", "Pending", "OK", "Failed"}
WriterPcs == {"Fresh", "Payload", "Metadata", "Notified"}
ConfigPcs == {"FirstAttempt", "Quiesce", "ReleaseActive",
               "Drain", "Callback", "Done"}

VARIABLES initialActive,
          initialPending,
          initialQueued,
          newRecord,
          activeRecord,
          pendingRecord,
          metaQueue,
          dataQueue,
          operationState,
          finishCount,
          metaPopCount,
          dataPopCount,
          writerPc,
          writePending,
          configPc,
          boundaryValid,
          configPrefix,
          drainRemaining,
          quiesced,
          activeReleased,
          callbackRecord

vars == <<initialActive, initialPending, initialQueued, newRecord,
          activeRecord, pendingRecord, metaQueue, dataQueue,
          operationState, finishCount, metaPopCount, dataPopCount,
          writerPc, writePending, configPc, boundaryValid, configPrefix,
          drainRemaining, quiesced, activeReleased, callbackRecord>>

RoleVariables == <<initialActive, initialPending, initialQueued, newRecord>>

QueueRecords(queue) == {queue[index] : index \in 1..Len(queue)}

PublishedMetaPayloads ==
  IF pendingRecord = NoRecord
  THEN metaQueue
  ELSE IF metaQueue = <<>>
       THEN <<>>
       ELSE Tail(metaQueue)

ExpectedDataQueue ==
  IF writerPc = "Payload"
  THEN Append(PublishedMetaPayloads, newRecord)
  ELSE PublishedMetaPayloads

NextPrefix(metaAfter) ==
  IF MovingBoundary THEN Len(metaAfter) ELSE configPrefix

NextRemaining(metaAfter) ==
  IF MovingBoundary THEN Len(metaAfter) ELSE drainRemaining - 1

Init ==
  \E active, pending, queued, fresh \in Records :
    /\ {active, pending, queued, fresh} = Records
    /\ initialActive = active
    /\ initialPending = pending
    /\ initialQueued = queued
    /\ newRecord = fresh
    /\ activeRecord = active
    /\ pendingRecord = pending
    /\ metaQueue = <<pending, queued>>
    /\ dataQueue = <<queued>>
    /\ operationState =
         [record \in Records |->
           IF record = active
           THEN "OK"
           ELSE IF record = fresh THEN "Fresh" ELSE "Pending"]
    /\ finishCount = [record \in Records |-> IF record = active THEN 1 ELSE 0]
    /\ metaPopCount = [record \in Records |-> 0]
    /\ dataPopCount = [record \in Records |-> 0]
    /\ writerPc = "Fresh"
    /\ writePending = FALSE
    /\ configPc = "FirstAttempt"
    /\ boundaryValid = FALSE
    /\ configPrefix = NoBoundary
    /\ drainRemaining = 0
    /\ quiesced = FALSE
    /\ activeReleased = FALSE
    /\ callbackRecord = NoRecord

ConfigFirstAttempt ==
  /\ configPc = "FirstAttempt"
  /\ ~boundaryValid
  /\ boundaryValid' = TRUE
  /\ configPrefix' = Len(metaQueue)
  /\ drainRemaining' = Len(metaQueue)
  /\ configPc' = "Quiesce"
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, metaQueue,
                  dataQueue, operationState, finishCount, metaPopCount,
                  dataPopCount, writerPc, writePending, quiesced,
                  activeReleased, callbackRecord>>

ConfigQuiesce ==
  /\ configPc = "Quiesce"
  /\ boundaryValid
  /\ ~quiesced
  /\ quiesced' = TRUE
  /\ configPc' = "ReleaseActive"
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, metaQueue,
                  dataQueue, operationState, finishCount, metaPopCount,
                  dataPopCount, writerPc, writePending, boundaryValid,
                  configPrefix, drainRemaining, activeReleased,
                  callbackRecord>>

ReleaseActive ==
  /\ configPc = "ReleaseActive"
  /\ quiesced
  /\ activeRecord = initialActive
  /\ operationState[initialActive] = "OK"
  /\ finishCount[initialActive] = 1
  /\ activeRecord' = NoRecord
  /\ activeReleased' = TRUE
  /\ configPc' = "Drain"
  /\ UNCHANGED <<RoleVariables, pendingRecord, metaQueue, dataQueue,
                  operationState, finishCount, metaPopCount, dataPopCount,
                  writerPc, writePending, boundaryValid, configPrefix,
                  drainRemaining, quiesced, callbackRecord>>

DrainPending ==
  LET record == pendingRecord
      metaAfter == Tail(metaQueue)
      poppedPayload == Head(dataQueue)
  IN
  /\ configPc = "Drain"
  /\ boundaryValid
  /\ quiesced
  /\ drainRemaining > 0
  /\ record # NoRecord
  /\ metaQueue # <<>>
  /\ Head(metaQueue) = record
  /\ operationState[record] = "Pending"
  /\ finishCount[record] = 0
  /\ metaPopCount[record] = 0
  /\ (~PendingPopsPayload \/ dataQueue # <<>>)
  /\ metaQueue' = metaAfter
  /\ dataQueue' = IF PendingPopsPayload THEN Tail(dataQueue) ELSE dataQueue
  /\ pendingRecord' = NoRecord
  /\ operationState' = [operationState EXCEPT ![record] = "Failed"]
  /\ finishCount' = [finishCount EXCEPT ![record] = @ + 1]
  /\ metaPopCount' = [metaPopCount EXCEPT ![record] = @ + 1]
  /\ dataPopCount' =
       IF PendingPopsPayload
       THEN [dataPopCount EXCEPT ![poppedPayload] = @ + 1]
       ELSE dataPopCount
  /\ configPrefix' = NextPrefix(metaAfter)
  /\ drainRemaining' = NextRemaining(metaAfter)
  /\ callbackRecord' = record
  /\ configPc' = "Callback"
  /\ UNCHANGED <<RoleVariables, activeRecord, writerPc, writePending,
                  boundaryValid, quiesced, activeReleased>>

DrainQueued ==
  LET record == Head(metaQueue)
      metaAfter == Tail(metaQueue)
  IN
  /\ configPc = "Drain"
  /\ boundaryValid
  /\ quiesced
  /\ drainRemaining > 0
  /\ pendingRecord = NoRecord
  /\ metaQueue # <<>>
  /\ dataQueue # <<>>
  /\ Head(dataQueue) = record
  /\ operationState[record] = "Pending"
  /\ finishCount[record] = 0
  /\ metaPopCount[record] = 0
  /\ dataPopCount[record] = 0
  /\ metaQueue' = metaAfter
  /\ dataQueue' = Tail(dataQueue)
  /\ operationState' = [operationState EXCEPT ![record] = "Failed"]
  /\ finishCount' = [finishCount EXCEPT ![record] = @ + 1]
  /\ metaPopCount' = [metaPopCount EXCEPT ![record] = @ + 1]
  /\ dataPopCount' = [dataPopCount EXCEPT ![record] = @ + 1]
  /\ configPrefix' = NextPrefix(metaAfter)
  /\ drainRemaining' = NextRemaining(metaAfter)
  /\ callbackRecord' = record
  /\ configPc' = "Callback"
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, writerPc,
                  writePending, boundaryValid, quiesced, activeReleased>>

WriterPushPayload ==
  /\ configPc = "Callback"
  /\ callbackRecord # NoRecord
  /\ boundaryValid
  /\ writerPc = "Fresh"
  /\ operationState[newRecord] = "Fresh"
  /\ dataQueue' = Append(dataQueue, newRecord)
  /\ operationState' = [operationState EXCEPT ![newRecord] = "Pending"]
  /\ writerPc' = "Payload"
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, metaQueue,
                  finishCount, metaPopCount, dataPopCount, writePending,
                  configPc, boundaryValid, configPrefix, drainRemaining,
                  quiesced, activeReleased, callbackRecord>>

WriterPublishMetadata ==
  /\ configPc = "Callback"
  /\ callbackRecord # NoRecord
  /\ boundaryValid
  /\ writerPc = "Payload"
  /\ metaQueue' = Append(metaQueue, newRecord)
  /\ writerPc' = "Metadata"
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, dataQueue,
                  operationState, finishCount, metaPopCount, dataPopCount,
                  writePending, configPc, boundaryValid, configPrefix,
                  drainRemaining, quiesced, activeReleased, callbackRecord>>

WriterPublishWrite ==
  /\ configPc = "Callback"
  /\ callbackRecord # NoRecord
  /\ boundaryValid
  /\ writerPc = "Metadata"
  /\ writerPc' = "Notified"
  /\ writePending' = TRUE
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, metaQueue,
                  dataQueue, operationState, finishCount, metaPopCount,
                  dataPopCount, configPc, boundaryValid, configPrefix,
                  drainRemaining, quiesced, activeReleased, callbackRecord>>

ReturnFromFinishCallback ==
  /\ configPc = "Callback"
  /\ callbackRecord # NoRecord
  /\ (callbackRecord # initialPending \/ writerPc = "Notified")
  /\ callbackRecord' = NoRecord
  /\ configPc' = "Drain"
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, metaQueue,
                  dataQueue, operationState, finishCount, metaPopCount,
                  dataPopCount, writerPc, writePending, boundaryValid,
                  configPrefix, drainRemaining, quiesced, activeReleased>>

CompleteDrain ==
  /\ configPc = "Drain"
  /\ boundaryValid
  /\ drainRemaining = 0
  /\ callbackRecord = NoRecord
  /\ configPc' = "Done"
  /\ boundaryValid' = FALSE
  /\ configPrefix' = NoBoundary
  /\ quiesced' = FALSE
  /\ UNCHANGED <<RoleVariables, activeRecord, pendingRecord, metaQueue,
                  dataQueue, operationState, finishCount, metaPopCount,
                  dataPopCount, writerPc, writePending, drainRemaining,
                  activeReleased, callbackRecord>>

TerminalStutter == configPc = "Done" /\ UNCHANGED vars

Next ==
  \/ ConfigFirstAttempt
  \/ ConfigQuiesce
  \/ ReleaseActive
  \/ DrainPending
  \/ DrainQueued
  \/ WriterPushPayload
  \/ WriterPublishMetadata
  \/ WriterPublishWrite
  \/ ReturnFromFinishCallback
  \/ CompleteDrain
  \/ TerminalStutter

Spec == Init /\ [][Next]_vars

FairSpec == Spec /\ WF_vars(Next)

TypeOK ==
  /\ initialActive \in Records
  /\ initialPending \in Records
  /\ initialQueued \in Records
  /\ newRecord \in Records
  /\ {initialActive, initialPending, initialQueued, newRecord} = Records
  /\ activeRecord \in Records \cup {NoRecord}
  /\ pendingRecord \in Records \cup {NoRecord}
  /\ metaQueue \in Seq(Records)
  /\ dataQueue \in Seq(Records)
  /\ operationState \in [Records -> OperationStates]
  /\ finishCount \in [Records -> 0..2]
  /\ metaPopCount \in [Records -> 0..2]
  /\ dataPopCount \in [Records -> 0..2]
  /\ writerPc \in WriterPcs
  /\ writePending \in BOOLEAN
  /\ configPc \in ConfigPcs
  /\ boundaryValid \in BOOLEAN
  /\ configPrefix \in 0..NoBoundary
  /\ drainRemaining \in 0..NoBoundary
  /\ quiesced \in BOOLEAN
  /\ activeReleased \in BOOLEAN
  /\ callbackRecord \in Records \cup {NoRecord}

QueueRecordsAreUnique ==
  /\ Cardinality(QueueRecords(metaQueue)) = Len(metaQueue)
  /\ Cardinality(QueueRecords(dataQueue)) = Len(dataQueue)

MetaDataAligned == dataQueue = ExpectedDataQueue

CompletionAtMostOnce == \A record \in Records : finishCount[record] <= 1

ActiveCompletionStable ==
  /\ operationState[initialActive] = "OK"
  /\ finishCount[initialActive] = 1
  /\ metaPopCount[initialActive] = 0
  /\ dataPopCount[initialActive] = 0
  /\ (activeReleased <=> activeRecord = NoRecord)

PendingConsumptionCorrect ==
  /\ dataPopCount[initialPending] = 0
  /\ finishCount[initialPending] = metaPopCount[initialPending]
  /\ (metaPopCount[initialPending] = 0 =>
        operationState[initialPending] = "Pending")
  /\ (metaPopCount[initialPending] = 1 =>
        operationState[initialPending] = "Failed")

QueuedConsumptionCorrect ==
  /\ metaPopCount[initialQueued] = dataPopCount[initialQueued]
  /\ finishCount[initialQueued] = metaPopCount[initialQueued]
  /\ (metaPopCount[initialQueued] = 0 =>
        operationState[initialQueued] = "Pending")
  /\ (metaPopCount[initialQueued] = 1 =>
        operationState[initialQueued] = "Failed")

PostBoundarySurvives ==
  /\ (writerPc \in {"Payload", "Metadata", "Notified"} =>
        /\ newRecord \in QueueRecords(dataQueue)
        /\ dataPopCount[newRecord] = 0
        /\ finishCount[newRecord] = 0
        /\ operationState[newRecord] = "Pending")
  /\ (writerPc \in {"Metadata", "Notified"} =>
        /\ newRecord \in QueueRecords(metaQueue)
        /\ metaPopCount[newRecord] = 0)
  /\ (writerPc = "Notified" => writePending)

FinalDrainCorrect ==
  configPc = "Done" =>
    /\ activeReleased
    /\ pendingRecord = NoRecord
    /\ metaQueue = <<newRecord>>
    /\ dataQueue = <<newRecord>>
    /\ writerPc = "Notified"
    /\ writePending
    /\ operationState[initialPending] = "Failed"
    /\ finishCount[initialPending] = 1
    /\ metaPopCount[initialPending] = 1
    /\ dataPopCount[initialPending] = 0
    /\ operationState[initialQueued] = "Failed"
    /\ finishCount[initialQueued] = 1
    /\ metaPopCount[initialQueued] = 1
    /\ dataPopCount[initialQueued] = 1
    /\ operationState[newRecord] = "Pending"
    /\ finishCount[newRecord] = 0
    /\ metaPopCount[newRecord] = 0
    /\ dataPopCount[newRecord] = 0

BoundaryStableStep ==
  (boundaryValid /\ boundaryValid') => configPrefix' = configPrefix

BoundaryDoesNotMove == [][BoundaryStableStep]_vars

ConfigEventuallyDrains == configPc # "Done" ~> configPc = "Done"

RecordSymmetry == Permutations(Records)

=============================================================================
