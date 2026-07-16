--------------------------- MODULE UartTxPipeline ---------------------------
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************
 * Bounded UART TX pipeline model.
 *
 * A record is one payload token and one logical WriteOperation.  The model
 * deliberately splits only the publication points at which another actor can
 * observe a different fact:
 *
 *   writer payload -> metadata -> WRITE event
 *   buffer copying -> copied privately -> pending published
 *   backend DMA started -> software commits active ownership
 *
 * Payload bytes, callback bodies, BLOCK waiter states, hardware-gate bits,
 * CONFIG, and DMA generations are separate refinement obligations.
 ***************************************************************************)

CONSTANTS Records,
          NoRecord,
          BreakPromoteBeforePublish,
          BreakTerminalDoubleFinish,
          BreakRetryConsumesRecord,
          BreakFinishWrongRecord

ASSUME /\ Cardinality(Records) = 3
       /\ NoRecord \notin Records
       /\ BreakPromoteBeforePublish \in BOOLEAN
       /\ BreakTerminalDoubleFinish \in BOOLEAN
       /\ BreakRetryConsumesRecord \in BOOLEAN
       /\ BreakFinishWrongRecord \in BOOLEAN

Blocks == {0, 1}
NoBlock == 2
BlockValues == Blocks \cup {NoBlock}

Events == {"WRITE", "TERMINAL"}
RecordStates == {"UNUSED", "RUNNING", "DONE", "FAILED"}
ServicePhases == {"Idle", "Take", "Handle", "Advance", "End"}
CopyTargets == {"None", "Pending"}
CopyPhases == {"Idle", "Copying", "Copied"}
CandidateKinds == {"None", "Pending"}
ActiveOrigins == {"None", "Pending"}

OtherBlock(block) == 1 - block

QueueRecords(q) == {q[i] : i \in 1..Len(q)}

DropHead(q) ==
  IF q = <<>> THEN <<>> ELSE Tail(q)

VARIABLES
  recordState,
  finishCount,
  producer,
  producerMetaPublished,
  metaQ,
  dataQ,
  events,
  owner,
  servicePhase,
  snapshot,
  activeRec,
  activeBlock,
  activeOrigin,
  pendingRec,
  pendingBlock,
  copyRec,
  copyBlock,
  copyTarget,
  copyPhase,
  candidateRec,
  candidateKind,
  startIssued,
  hwRecord,
  retryRec,
  pendingPublished

vars == <<
  recordState,
  finishCount,
  producer,
  producerMetaPublished,
  metaQ,
  dataQ,
  events,
  owner,
  servicePhase,
  snapshot,
  activeRec,
  activeBlock,
  activeOrigin,
  pendingRec,
  pendingBlock,
  copyRec,
  copyBlock,
  copyTarget,
  copyPhase,
  candidateRec,
  candidateKind,
  startIssued,
  hwRecord,
  retryRec,
  pendingPublished
>>

CopyIdle == copyRec = NoRecord
CandidateIdle == candidateRec = NoRecord

CanSelectPending ==
  /\ activeRec = NoRecord
  /\ pendingRec # NoRecord
  /\ CopyIdle
  /\ CandidateIdle
  /\ ~startIssued

CanCopyPending ==
  /\ pendingRec = NoRecord
  /\ metaQ # <<>>
  /\ CopyIdle
  /\ CandidateIdle
  /\ ~startIssued

AdvanceNeeded ==
  CanSelectPending \/ CanCopyPending

PendingPayloadRemoved ==
  \/ pendingRec # NoRecord
  \/ (copyRec # NoRecord /\ copyTarget = "Pending" /\ copyPhase = "Copied")

PublishedPayloads ==
  IF PendingPayloadRemoved
  THEN DropHead(metaQ)
  ELSE metaQ

ProducerPayloadSuffix ==
  IF producer # NoRecord /\ ~producerMetaPublished
  THEN <<producer>>
  ELSE <<>>

Init ==
  /\ recordState = [r \in Records |-> "UNUSED"]
  /\ finishCount = [r \in Records |-> 0]
  /\ producer = NoRecord
  /\ producerMetaPublished = FALSE
  /\ metaQ = <<>>
  /\ dataQ = <<>>
  /\ events = {}
  /\ owner = FALSE
  /\ servicePhase = "Idle"
  /\ snapshot = {}
  /\ activeRec = NoRecord
  \* The first pending copy uses physical block 0.  Block 1 is a notional
  \* already-active sentinel until the first STARTED commit.
  /\ activeBlock = 1
  /\ activeOrigin = "None"
  /\ pendingRec = NoRecord
  /\ pendingBlock = NoBlock
  /\ copyRec = NoRecord
  /\ copyBlock = NoBlock
  /\ copyTarget = "None"
  /\ copyPhase = "Idle"
  /\ candidateRec = NoRecord
  /\ candidateKind = "None"
  /\ startIssued = FALSE
  /\ hwRecord = NoRecord
  /\ retryRec = NoRecord
  /\ pendingPublished = {}

(***************************************************************************
 * The WritePort producer is unique, but it may advance while a TX service
 * owner is active.  Metadata is the record-publication boundary.
 ***************************************************************************)

WriteCopyPayload(r) ==
  /\ r \in Records
  /\ recordState[r] = "UNUSED"
  /\ producer = NoRecord
  /\ recordState' = [recordState EXCEPT ![r] = "RUNNING"]
  /\ producer' = r
  /\ producerMetaPublished' = FALSE
  /\ dataQ' = Append(dataQ, r)
  /\ UNCHANGED <<finishCount, metaQ, events, owner, servicePhase, snapshot,
                  activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, copyPhase,
                  candidateRec, candidateKind, startIssued, hwRecord,
                  retryRec, pendingPublished>>

WritePublishMetadata ==
  /\ producer # NoRecord
  /\ ~producerMetaPublished
  /\ metaQ' = Append(metaQ, producer)
  /\ producerMetaPublished' = TRUE
  /\ UNCHANGED <<recordState, finishCount, producer, dataQ, events, owner,
                  servicePhase, snapshot, activeRec, activeBlock,
                  activeOrigin, pendingRec, pendingBlock, copyRec, copyBlock,
                  copyTarget, copyPhase, candidateRec, candidateKind,
                  startIssued, hwRecord, retryRec, pendingPublished>>

WriteRaise ==
  /\ producer # NoRecord
  /\ producerMetaPublished
  /\ events' = events \cup {"WRITE"}
  /\ producer' = NoRecord
  /\ producerMetaPublished' = FALSE
  /\ UNCHANGED <<recordState, finishCount, metaQ, dataQ, owner,
                  servicePhase, snapshot, activeRec, activeBlock,
                  activeOrigin, pendingRec, pendingBlock, copyRec, copyBlock,
                  copyTarget, copyPhase, candidateRec, candidateKind,
                  startIssued, hwRecord, retryRec, pendingPublished>>

(***************************************************************************
 * Coalescing service ownership.  A round finishes all local pipeline work
 * before taking an event that arrived during the round.
 ***************************************************************************)

ServiceClaim ==
  /\ ~owner
  /\ events # {}
  /\ owner' = TRUE
  /\ servicePhase' = "Take"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, snapshot,
                  activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, copyPhase,
                  candidateRec, candidateKind, startIssued, hwRecord,
                  retryRec, pendingPublished>>

ServiceTake ==
  /\ owner
  /\ servicePhase = "Take"
  /\ snapshot = {}
  /\ events # {}
  /\ snapshot' = events
  /\ events' = {}
  /\ servicePhase' = "Handle"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, owner, activeRec,
                  activeBlock, activeOrigin, pendingRec, pendingBlock,
                  copyRec, copyBlock, copyTarget, copyPhase, candidateRec,
                  candidateKind, startIssued, hwRecord, retryRec,
                  pendingPublished>>

ServiceHandle ==
  LET terminal == "TERMINAL" \in snapshot
      rec == activeRec
  IN
  /\ owner
  /\ servicePhase = "Handle"
  /\ snapshot # {}
  /\ terminal => rec # NoRecord
  /\ recordState' =
       IF terminal /\ BreakTerminalDoubleFinish
       THEN [recordState EXCEPT ![rec] = "DONE"]
       ELSE recordState
  /\ finishCount' =
       IF terminal /\ BreakTerminalDoubleFinish
       THEN [finishCount EXCEPT ![rec] = @ + 1]
       ELSE finishCount
  /\ activeRec' = IF terminal THEN NoRecord ELSE activeRec
  /\ activeOrigin' = IF terminal THEN "None" ELSE activeOrigin
  /\ snapshot' = {}
  /\ servicePhase' = "Advance"
  /\ UNCHANGED <<producer, producerMetaPublished, metaQ, dataQ, events,
                  owner, activeBlock, pendingRec, pendingBlock, copyRec,
                  copyBlock, copyTarget, copyPhase, candidateRec,
                  candidateKind, startIssued, hwRecord, retryRec,
                  pendingPublished>>

FinishRound ==
  /\ owner
  /\ servicePhase = "Advance"
  /\ CopyIdle
  /\ CandidateIdle
  /\ ~startIssued
  /\ ~AdvanceNeeded
  /\ servicePhase' = "End"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, owner,
                  snapshot, activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, copyPhase,
                  candidateRec, candidateKind, startIssued, hwRecord,
                  retryRec, pendingPublished>>

OwnerNextRound ==
  /\ owner
  /\ servicePhase = "End"
  /\ events # {}
  /\ servicePhase' = "Take"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, owner,
                  snapshot, activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, copyPhase,
                  candidateRec, candidateKind, startIssued, hwRecord,
                  retryRec, pendingPublished>>

OwnerRelease ==
  /\ owner
  /\ servicePhase = "End"
  /\ events = {}
  /\ owner' = FALSE
  /\ servicePhase' = "Idle"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, snapshot,
                  activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, copyPhase,
                  candidateRec, candidateKind, startIssued, hwRecord,
                  retryRec, pendingPublished>>

(***************************************************************************
 * Buffer copy and publication.  Pending payload consumption precedes the
 * pending-valid publication, but service ownership is retained across both.
 ***************************************************************************)

BeginPendingCopy ==
  /\ owner
  /\ servicePhase = "Advance"
  /\ CanCopyPending
  /\ dataQ # <<>>
  /\ Head(dataQ) = Head(metaQ)
  /\ copyRec' = Head(metaQ)
  /\ copyBlock' = OtherBlock(activeBlock)
  /\ copyTarget' = "Pending"
  /\ copyPhase' = "Copying"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, owner,
                  servicePhase, snapshot, activeRec, activeBlock,
                  activeOrigin, pendingRec, pendingBlock, candidateRec,
                  candidateKind, startIssued, hwRecord, retryRec,
                  pendingPublished>>

FinishCopy ==
  /\ owner
  /\ servicePhase = "Advance"
  /\ copyRec # NoRecord
  /\ copyPhase = "Copying"
  /\ dataQ # <<>>
  /\ Head(dataQ) = copyRec
  /\ copyPhase' = "Copied"
  /\ dataQ' = Tail(dataQ)
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, events, owner, servicePhase,
                  snapshot, activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, candidateRec,
                  candidateKind, startIssued, hwRecord, retryRec,
                  pendingPublished>>

PublishPending ==
  /\ owner
  /\ servicePhase = "Advance"
  /\ copyRec # NoRecord
  /\ copyTarget = "Pending"
  /\ copyPhase = "Copied"
  /\ pendingRec' = copyRec
  /\ pendingBlock' = copyBlock
  /\ pendingPublished' = pendingPublished \cup {copyRec}
  /\ copyRec' = NoRecord
  /\ copyBlock' = NoBlock
  /\ copyTarget' = "None"
  /\ copyPhase' = "Idle"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, owner,
                  servicePhase, snapshot, activeRec, activeBlock,
                  activeOrigin, candidateRec, candidateKind, startIssued,
                  hwRecord, retryRec>>

SelectPending ==
  /\ owner
  /\ servicePhase = "Advance"
  /\ CanSelectPending
  /\ candidateRec' = pendingRec
  /\ candidateKind' = "Pending"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, owner,
                  servicePhase, snapshot, activeRec, activeBlock,
                  activeOrigin, pendingRec, pendingBlock, copyRec, copyBlock,
                  copyTarget, copyPhase, startIssued, hwRecord, retryRec,
                  pendingPublished>>

(***************************************************************************
 * DMA start results.  STARTED is split so a hardware terminal can be raised
 * after DMA enable but before software consumes the queue record.
 ***************************************************************************)

BackendRetry ==
  LET rec == candidateRec
      consume == BreakRetryConsumesRecord
  IN
  /\ owner
  /\ servicePhase = "Advance"
  /\ rec # NoRecord
  /\ candidateKind = "Pending"
  /\ pendingRec = rec
  /\ ~startIssued
  /\ metaQ # <<>>
  /\ Head(metaQ) = rec
  /\ metaQ' = IF consume THEN Tail(metaQ) ELSE metaQ
  /\ dataQ' = dataQ
  /\ pendingRec' =
       IF consume THEN NoRecord ELSE pendingRec
  /\ pendingBlock' =
       IF consume THEN NoBlock ELSE pendingBlock
  /\ retryRec' = rec
  /\ candidateRec' = NoRecord
  /\ candidateKind' = "None"
  /\ servicePhase' = "End"
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, events, owner, snapshot, activeRec,
                  activeBlock, activeOrigin, copyRec, copyBlock, copyTarget,
                  copyPhase, startIssued, hwRecord, pendingPublished>>

DispatchRetry ==
  /\ retryRec # NoRecord
  /\ events' = events \cup {"WRITE"}
  /\ retryRec' = NoRecord
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, owner, servicePhase,
                  snapshot, activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, copyPhase,
                  candidateRec, candidateKind, startIssued, hwRecord,
                  pendingPublished>>

BackendFailed ==
  LET rec == candidateRec
  IN
  /\ owner
  /\ servicePhase = "Advance"
  /\ rec # NoRecord
  /\ candidateKind = "Pending"
  /\ pendingRec = rec
  /\ ~startIssued
  /\ recordState[rec] = "RUNNING"
  /\ finishCount[rec] = 0
  /\ metaQ # <<>>
  /\ Head(metaQ) = rec
  /\ recordState' = [recordState EXCEPT ![rec] = "FAILED"]
  /\ finishCount' = [finishCount EXCEPT ![rec] = @ + 1]
  /\ metaQ' = Tail(metaQ)
  /\ dataQ' = dataQ
  /\ pendingRec' = NoRecord
  /\ pendingBlock' = NoBlock
  /\ candidateRec' = NoRecord
  /\ candidateKind' = "None"
  /\ servicePhase' = "End"
  /\ UNCHANGED <<producer, producerMetaPublished, events, owner, snapshot,
                  activeRec, activeBlock, activeOrigin, copyRec, copyBlock, copyTarget,
                  copyPhase, startIssued, hwRecord, retryRec,
                  pendingPublished>>

BackendStarted ==
  /\ owner
  /\ servicePhase = "Advance"
  /\ candidateRec # NoRecord
  /\ candidateKind = "Pending"
  /\ pendingRec = candidateRec
  /\ ~startIssued
  /\ hwRecord = NoRecord
  /\ startIssued' = TRUE
  /\ hwRecord' = candidateRec
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, events, owner,
                  servicePhase, snapshot, activeRec, activeBlock,
                  activeOrigin, pendingRec, pendingBlock, copyRec, copyBlock,
                  copyTarget, copyPhase, candidateRec, candidateKind,
                  retryRec, pendingPublished>>

CommitStarted ==
  LET rec == candidateRec
  IN
  /\ owner
  /\ servicePhase = "Advance"
  /\ startIssued
  /\ rec # NoRecord
  /\ candidateKind = "Pending"
  /\ pendingRec = rec
  /\ recordState[rec] = "RUNNING"
  /\ finishCount[rec] = 0
  /\ metaQ # <<>>
  /\ Head(metaQ) = rec
  /\ recordState' = [recordState EXCEPT ![rec] = "DONE"]
  /\ finishCount' = [finishCount EXCEPT ![rec] = @ + 1]
  /\ metaQ' = Tail(metaQ)
  /\ dataQ' = dataQ
  /\ activeRec' = rec
  /\ activeBlock' = pendingBlock
  /\ activeOrigin' = "Pending"
  /\ pendingRec' = NoRecord
  /\ pendingBlock' = NoBlock
  /\ candidateRec' = NoRecord
  /\ candidateKind' = "None"
  /\ startIssued' = FALSE
  /\ UNCHANGED <<producer, producerMetaPublished, events, owner,
                  servicePhase, snapshot, copyRec, copyBlock, copyTarget,
                  copyPhase, hwRecord, retryRec, pendingPublished>>

CommitStartedWrongRecord(target) ==
  LET rec == candidateRec
  IN
  /\ BreakFinishWrongRecord
  /\ target \in Records
  /\ target # rec
  /\ owner
  /\ servicePhase = "Advance"
  /\ startIssued
  /\ rec # NoRecord
  /\ candidateKind = "Pending"
  /\ pendingRec = rec
  /\ recordState[rec] = "RUNNING"
  /\ finishCount[rec] = 0
  /\ recordState[target] = "RUNNING"
  /\ finishCount[target] = 0
  /\ metaQ # <<>>
  /\ Head(metaQ) = rec
  /\ target \in QueueRecords(Tail(metaQ))
  /\ recordState' = [recordState EXCEPT ![target] = "DONE"]
  /\ finishCount' = [finishCount EXCEPT ![target] = @ + 1]
  /\ metaQ' = Tail(metaQ)
  /\ dataQ' = dataQ
  /\ activeRec' = rec
  /\ activeBlock' = pendingBlock
  /\ activeOrigin' = "Pending"
  /\ pendingRec' = NoRecord
  /\ pendingBlock' = NoBlock
  /\ candidateRec' = NoRecord
  /\ candidateKind' = "None"
  /\ startIssued' = FALSE
  /\ UNCHANGED <<producer, producerMetaPublished, events, owner,
                  servicePhase, snapshot, copyRec, copyBlock, copyTarget,
                  copyPhase, hwRecord, retryRec, pendingPublished>>

PublishTerminal ==
  /\ hwRecord # NoRecord
  /\ events' = events \cup {"TERMINAL"}
  /\ hwRecord' = NoRecord
  /\ UNCHANGED <<recordState, finishCount, producer,
                  producerMetaPublished, metaQ, dataQ, owner, servicePhase,
                  snapshot, activeRec, activeBlock, activeOrigin, pendingRec,
                  pendingBlock, copyRec, copyBlock, copyTarget, copyPhase,
                  candidateRec, candidateKind, startIssued, retryRec,
                  pendingPublished>>

(***************************************************************************
 * Four independent negative switches.  Each broken action retains enough
 * identity to make its intended invariant fail immediately.
 ***************************************************************************)

PromoteUnpublishedPending ==
  LET rec == copyRec IN
  /\ BreakPromoteBeforePublish
  /\ owner
  /\ servicePhase = "Advance"
  /\ rec # NoRecord
  /\ copyTarget = "Pending"
  /\ copyPhase = "Copied"
  /\ activeRec # NoRecord
  /\ "TERMINAL" \in events
  /\ hwRecord = NoRecord
  /\ metaQ # <<>>
  /\ Head(metaQ) = rec
  /\ recordState[rec] = "RUNNING"
  /\ finishCount[rec] = 0
  /\ recordState' = [recordState EXCEPT ![rec] = "DONE"]
  /\ finishCount' = [finishCount EXCEPT ![rec] = @ + 1]
  /\ metaQ' = Tail(metaQ)
  /\ events' = events \ {"TERMINAL"}
  /\ activeRec' = rec
  /\ activeBlock' = copyBlock
  /\ activeOrigin' = "Pending"
  /\ copyRec' = NoRecord
  /\ copyBlock' = NoBlock
  /\ copyTarget' = "None"
  /\ copyPhase' = "Idle"
  /\ hwRecord' = rec
  /\ UNCHANGED <<producer, producerMetaPublished, dataQ, owner,
                  servicePhase, snapshot, pendingRec, pendingBlock,
                  candidateRec, candidateKind, startIssued, retryRec,
                  pendingPublished>>

Next ==
  \/ \E r \in Records : WriteCopyPayload(r)
  \/ WritePublishMetadata
  \/ WriteRaise
  \/ ServiceClaim
  \/ ServiceTake
  \/ ServiceHandle
  \/ BeginPendingCopy
  \/ FinishCopy
  \/ PublishPending
  \/ SelectPending
  \/ BackendRetry
  \/ DispatchRetry
  \/ BackendFailed
  \/ BackendStarted
  \/ CommitStarted
  \/ \E target \in Records : CommitStartedWrongRecord(target)
  \/ PublishTerminal
  \/ PromoteUnpublishedPending
  \/ FinishRound
  \/ OwnerNextRound
  \/ OwnerRelease

Spec == Init /\ [][Next]_vars

NoBackendFailures == \A r \in Records : recordState[r] # "FAILED"

FairSpec ==
  /\ Spec
  /\ []NoBackendFailures
  /\ WF_vars(WritePublishMetadata)
  /\ WF_vars(WriteRaise)
  /\ WF_vars(ServiceClaim)
  /\ WF_vars(ServiceTake)
  /\ WF_vars(ServiceHandle)
  /\ WF_vars(BeginPendingCopy)
  /\ WF_vars(FinishCopy)
  /\ WF_vars(PublishPending)
  /\ WF_vars(SelectPending)
  /\ SF_vars(BackendStarted)
  /\ WF_vars(CommitStarted)
  /\ WF_vars(PublishTerminal)
  /\ WF_vars(DispatchRetry)
  /\ WF_vars(FinishRound)
  /\ WF_vars(OwnerNextRound)
  /\ WF_vars(OwnerRelease)

RecordSymmetry == Permutations(Records)

(***************************************************************************
 * Safety properties.
 ***************************************************************************)

TypeOK ==
  /\ recordState \in [Records -> RecordStates]
  /\ finishCount \in [Records -> 0..2]
  /\ producer \in Records \cup {NoRecord}
  /\ producerMetaPublished \in BOOLEAN
  /\ metaQ \in Seq(Records)
  /\ dataQ \in Seq(Records)
  /\ events \subseteq Events
  /\ owner \in BOOLEAN
  /\ servicePhase \in ServicePhases
  /\ snapshot \subseteq Events
  /\ activeRec \in Records \cup {NoRecord}
  /\ activeBlock \in Blocks
  /\ activeOrigin \in ActiveOrigins
  /\ pendingRec \in Records \cup {NoRecord}
  /\ pendingBlock \in BlockValues
  /\ copyRec \in Records \cup {NoRecord}
  /\ copyBlock \in BlockValues
  /\ copyTarget \in CopyTargets
  /\ copyPhase \in CopyPhases
  /\ candidateRec \in Records \cup {NoRecord}
  /\ candidateKind \in CandidateKinds
  /\ startIssued \in BOOLEAN
  /\ hwRecord \in Records \cup {NoRecord}
  /\ retryRec \in Records \cup {NoRecord}
  /\ pendingPublished \subseteq Records

QueueHasNoDuplicates ==
  /\ Cardinality(QueueRecords(metaQ)) = Len(metaQ)
  /\ Cardinality(QueueRecords(dataQ)) = Len(dataQ)

OwnerStateConsistent ==
  /\ (~owner <=> servicePhase = "Idle")
  /\ (~owner => snapshot = {})
  /\ (snapshot # {} <=> servicePhase = "Handle")
  /\ (copyRec # NoRecord => owner /\ servicePhase = "Advance")
  /\ (candidateRec # NoRecord => owner /\ servicePhase = "Advance")
  /\ (startIssued => owner /\ servicePhase = "Advance")

ProducerStateConsistent ==
  /\ (producer = NoRecord => ~producerMetaPublished)
  /\ (producer # NoRecord => recordState[producer] # "UNUSED")
  /\ (producer # NoRecord /\ producerMetaPublished =>
        producer \in QueueRecords(metaQ) \/
        recordState[producer] \in {"DONE", "FAILED"})
  /\ (producer # NoRecord /\ ~producerMetaPublished =>
        producer \notin QueueRecords(metaQ))

PayloadMetadataAligned ==
  dataQ = PublishedPayloads \o ProducerPayloadSuffix

PendingStateConsistent ==
  /\ (pendingRec = NoRecord <=> pendingBlock = NoBlock)
  /\ (pendingRec # NoRecord =>
        /\ metaQ # <<>>
        /\ Head(metaQ) = pendingRec
        /\ recordState[pendingRec] = "RUNNING")

CopyStateConsistent ==
  /\ (copyRec = NoRecord <=>
        copyBlock = NoBlock /\ copyTarget = "None" /\ copyPhase = "Idle")
  /\ (copyRec # NoRecord =>
        /\ owner
        /\ servicePhase = "Advance"
        /\ metaQ # <<>>
        /\ Head(metaQ) = copyRec
        /\ recordState[copyRec] = "RUNNING"
        /\ copyTarget = "Pending"
        /\ copyPhase # "Idle")
  /\ (copyRec # NoRecord /\
      ~(copyTarget = "Pending" /\ copyPhase = "Copied") =>
        dataQ # <<>> /\ Head(dataQ) = copyRec)

CandidateStateConsistent ==
  /\ (candidateRec = NoRecord <=> candidateKind = "None")
  /\ (candidateRec # NoRecord =>
        /\ owner
        /\ servicePhase = "Advance"
        /\ copyRec = NoRecord
        /\ metaQ # <<>>
        /\ Head(metaQ) = candidateRec
        /\ recordState[candidateRec] = "RUNNING"
        /\ candidateKind = "Pending"
        /\ pendingRec = candidateRec)

ActiveStateConsistent ==
  /\ (activeRec = NoRecord <=> activeOrigin = "None")
  /\ (activeRec # NoRecord =>
        /\ recordState[activeRec] = "DONE"
        /\ finishCount[activeRec] = 1
        /\ activeRec \notin QueueRecords(metaQ)
        /\ activeRec \notin QueueRecords(dataQ)
        /\ activeRec # pendingRec
        /\ activeRec # copyRec
        /\ activeRec # candidateRec)

BufferBlocksDisjoint ==
  /\ (activeRec # NoRecord /\ pendingRec # NoRecord =>
        activeBlock # pendingBlock)
  /\ (activeRec # NoRecord /\ copyRec # NoRecord /\
      copyTarget = "Pending" => activeBlock # copyBlock)
  /\ ~(pendingRec # NoRecord /\ copyRec # NoRecord)

HardwareStateConsistent ==
  /\ (startIssued => candidateRec # NoRecord)
  /\ (hwRecord # NoRecord =>
        hwRecord = activeRec \/ (startIssued /\ hwRecord = candidateRec))
  /\ (activeRec # NoRecord =>
        hwRecord = activeRec \/
        "TERMINAL" \in events \/ "TERMINAL" \in snapshot)

StartWindowConsistent ==
  startIssued =>
    /\ candidateRec # NoRecord
    /\ recordState[candidateRec] = "RUNNING"
    /\ finishCount[candidateRec] = 0
    /\ (hwRecord = candidateRec \/ "TERMINAL" \in events)

FinishAtMostOnce ==
  \A r \in Records : finishCount[r] <= 1

FinishMatchesState ==
  \A r \in Records :
    /\ (recordState[r] \in {"UNUSED", "RUNNING"} => finishCount[r] = 0)
    /\ (recordState[r] \in {"DONE", "FAILED"} => finishCount[r] = 1)

CompletionIdentityMatchesActiveRecordStep ==
  (activeRec' # NoRecord /\ activeRec' # activeRec) =>
    /\ recordState'[activeRec'] = "DONE"
    /\ finishCount'[activeRec'] = finishCount[activeRec'] + 1
    /\ \A r \in Records \ {activeRec'} : finishCount'[r] = finishCount[r]

CompletionIdentityMatchesActiveRecord ==
  [][CompletionIdentityMatchesActiveRecordStep]_vars

RunningRecordHasCarrier ==
  \A r \in Records :
    recordState[r] = "RUNNING" =>
      r = producer \/ r \in QueueRecords(metaQ) \/ r = retryRec

RetryKeepsRecordOwned ==
  retryRec = NoRecord \/
  retryRec \in QueueRecords(metaQ) \/
  recordState[retryRec] \in {"DONE", "FAILED"}

PromotedPendingWasPublished ==
  activeOrigin # "Pending" \/ activeRec \in pendingPublished

WriterPublicationPreservesOwnerStep ==
  (producer' # producer \/
   producerMetaPublished' # producerMetaPublished) =>
    /\ owner' = owner
    /\ servicePhase' = servicePhase
    /\ snapshot' = snapshot

WriterPublicationPreservesOwner ==
  [][WriterPublicationPreservesOwnerStep]_vars

NoWriterPublicationWhileOwnerStep ==
  owner =>
    /\ producer' = producer
    /\ producerMetaPublished' = producerMetaPublished

NoWriterPublicationWhileOwnerWitness ==
  [][NoWriterPublicationWhileOwnerStep]_vars

TerminalDoesNotFinishStep ==
  (servicePhase = "Handle" /\ "TERMINAL" \in snapshot) =>
    finishCount' = finishCount

TerminalDoesNotFinishOperation ==
  [][TerminalDoesNotFinishStep]_vars

FailedDoesNotFlipActiveBlockStep ==
  (\E r \in Records :
      recordState[r] = "RUNNING" /\ recordState'[r] = "FAILED")
      => activeBlock' = activeBlock

FailedDoesNotFlipActiveBlock ==
  [][FailedDoesNotFlipActiveBlockStep]_vars

RunningWriteEventuallyResolves ==
  \A r \in Records : recordState[r] = "RUNNING" ~> finishCount[r] = 1

PrivateCopyEventuallyPublishes == copyRec # NoRecord ~> copyRec = NoRecord

RetryEventuallyRedispatches == retryRec # NoRecord ~> retryRec = NoRecord

OwnerEventuallyReleases == owner ~> ~owner

=============================================================================
