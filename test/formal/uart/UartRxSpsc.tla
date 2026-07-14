---------------------------- MODULE UartRxSpsc ----------------------------
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************
 * Bounded UART RX SPSC / ReadPort protocol.
 *
 * One ISR actor is the only queue producer. Its Push and
 * ProcessPendingReads steps are separate so a clear may interleave between
 * publication and notification. Reader, clearer, and pending-read
 * completion share one abstract consumer owner; TX service is intentionally
 * absent.
 *
 * Clear captures the queue sequence once and removes exactly that prefix.
 * An empty initial snapshot releases directly to IDLE without another Size
 * observation. A nonempty snapshot is dropped before the final Size
 * observation and busy-state store. The producer may append throughout either
 * path; a push after the relevant observation is preserved but can leave a
 * non-empty queue in IDLE instead of EVENT.
 *
 * The bounded model assumes successful pushes (capacity >= MaxItems) and one
 * one-byte ordinary read. It checks the ownership and queue-order protocol,
 * not queue-full drop policy or the complete ReadOperation lifecycle. The
 * abstract consumer claim requires the ReadPort single-logical-consumer
 * contract; it does not prove that two concurrent front-end callers are
 * serialized by the current ordinary-read implementation.
 *
 * MovingClearBoundary is a negative switch. When enabled, clear discards the
 * queue size observed at its later drop step instead of its fixed snapshot.
 *************************************************************************)

CONSTANTS MaxItems,
          MovingClearBoundary

ASSUME /\ MaxItems \in Nat \ {0}
       /\ MovingClearBoundary \in BOOLEAN

Items == 1..MaxItems

ProducerPcs == {"Ready", "Notify", "Completing", "Done"}
ReaderPcs == {"Ready", "Claimed", "Waiting", "Done"}
ClearPcs ==
  {"Ready", "Claimed", "EmptyObserved", "Snapshotted", "Dropped",
   "ReleaseObserved", "Done"}
Owners == {"None", "Reader", "Clear", "Pending"}
PortStates == {"IDLE", "EVENT", "PENDING", "CLEARING"}

VARIABLES queue,
          produced,
          removals,
          delivered,
          clearRemoved,
          nextItem,
          producerPc,
          readerPc,
          clearPc,
          consumerOwner,
          portState,
          clearSnapshot,
          clearCutoff,
          postSnapshotPush,
          pushDuringClearing,
          releaseSawData,
          clearJustReleased

vars == <<queue, produced, removals, delivered, clearRemoved,
          nextItem, producerPc, readerPc, clearPc, consumerOwner,
          portState, clearSnapshot, clearCutoff, postSnapshotPush,
          pushDuringClearing, releaseSawData, clearJustReleased>>

SeqSet(seq) == {seq[index] : index \in 1..Len(seq)}

NoDuplicates(seq) == Cardinality(SeqSet(seq)) = Len(seq)

IsPrefix(prefix, seq) ==
  /\ Len(prefix) <= Len(seq)
  /\ \A index \in 1..Len(prefix) : prefix[index] = seq[index]

TakePrefix(seq, count) ==
  IF count = 0 THEN <<>> ELSE SubSeq(seq, 1, count)

DropPrefix(seq, count) ==
  IF count = 0 THEN seq
  ELSE IF count = Len(seq) THEN <<>>
  ELSE SubSeq(seq, count + 1, Len(seq))

HasItemAfter(seq, cutoff) ==
  \E index \in 1..Len(seq) : seq[index] > cutoff

Init ==
  /\ queue = <<>>
  /\ produced = <<>>
  /\ removals = <<>>
  /\ delivered = <<>>
  /\ clearRemoved = <<>>
  /\ nextItem = 1
  /\ producerPc = "Ready"
  /\ readerPc = "Ready"
  /\ clearPc = "Ready"
  /\ consumerOwner = "None"
  /\ portState = "IDLE"
  /\ clearSnapshot = <<>>
  /\ clearCutoff = 0
  /\ postSnapshotPush = FALSE
  /\ pushDuringClearing = FALSE
  /\ releaseSawData = FALSE
  /\ clearJustReleased = FALSE

ProducerPush ==
  /\ producerPc = "Ready"
  /\ nextItem <= MaxItems
  /\ queue' = Append(queue, nextItem)
  /\ produced' = Append(produced, nextItem)
  /\ nextItem' = nextItem + 1
  /\ producerPc' = "Notify"
  /\ postSnapshotPush' =
       (postSnapshotPush \/
        clearPc \in {"EmptyObserved", "Snapshotted", "Dropped",
                     "ReleaseObserved"})
  /\ pushDuringClearing' = (pushDuringClearing \/ portState = "CLEARING")
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<removals, delivered, clearRemoved, readerPc, clearPc,
                  consumerOwner, portState, clearSnapshot, clearCutoff,
                  releaseSawData>>

ProducerFinish ==
  /\ producerPc = "Ready"
  /\ nextItem > MaxItems
  /\ producerPc' = "Done"
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, readerPc, clearPc, consumerOwner, portState,
                  clearSnapshot, clearCutoff, postSnapshotPush,
                  pushDuringClearing, releaseSawData, clearJustReleased>>

(***************************************************************************
 * ProcessPendingReads claims pending completion before it dequeues. This
 * keeps the ISR producer from becoming a second simultaneous consumer.
 *************************************************************************)
ProducerClaimPending ==
  /\ producerPc = "Notify"
  /\ portState = "PENDING"
  /\ readerPc = "Waiting"
  /\ consumerOwner = "None"
  /\ Len(queue) > 0
  /\ producerPc' = "Completing"
  /\ consumerOwner' = "Pending"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, readerPc, clearPc, portState, clearSnapshot,
                  clearCutoff, postSnapshotPush, pushDuringClearing,
                  releaseSawData>>

ProducerCompletePending ==
  /\ producerPc = "Completing"
  /\ consumerOwner = "Pending"
  /\ portState = "PENDING"
  /\ readerPc = "Waiting"
  /\ Len(queue) > 0
  /\ queue' = Tail(queue)
  /\ removals' = Append(removals, Head(queue))
  /\ delivered' = Append(delivered, Head(queue))
  /\ producerPc' = "Ready"
  /\ readerPc' = "Done"
  /\ consumerOwner' = "None"
  /\ portState' = "IDLE"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<produced, clearRemoved, nextItem, clearPc,
                  clearSnapshot, clearCutoff, postSnapshotPush,
                  pushDuringClearing, releaseSawData>>

ProducerPublishEvent ==
  /\ producerPc = "Notify"
  /\ portState = "IDLE"
  /\ consumerOwner = "None"
  /\ producerPc' = "Ready"
  /\ portState' = "EVENT"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, readerPc, clearPc, consumerOwner, clearSnapshot,
                  clearCutoff, postSnapshotPush, pushDuringClearing,
                  releaseSawData>>

ProducerNotificationDeferred ==
  /\ producerPc = "Notify"
  /\ ~((portState = "PENDING") /\ (readerPc = "Waiting") /\
       (consumerOwner = "None") /\ (Len(queue) > 0))
  /\ ~((portState = "IDLE") /\ (consumerOwner = "None"))
  /\ producerPc' = "Ready"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, readerPc, clearPc, consumerOwner, portState,
                  clearSnapshot, clearCutoff, postSnapshotPush,
                  pushDuringClearing, releaseSawData>>

ReaderClaim ==
  /\ readerPc = "Ready"
  /\ consumerOwner = "None"
  /\ portState \in {"IDLE", "EVENT"}
  /\ readerPc' = "Claimed"
  /\ consumerOwner' = "Reader"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, producerPc, clearPc, portState, clearSnapshot,
                  clearCutoff, postSnapshotPush, pushDuringClearing,
                  releaseSawData>>

ReaderConsume ==
  /\ readerPc = "Claimed"
  /\ consumerOwner = "Reader"
  /\ Len(queue) > 0
  /\ queue' = Tail(queue)
  /\ removals' = Append(removals, Head(queue))
  /\ delivered' = Append(delivered, Head(queue))
  /\ readerPc' = "Done"
  /\ consumerOwner' = "None"
  /\ portState' = "IDLE"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<produced, clearRemoved, nextItem, producerPc, clearPc,
                  clearSnapshot, clearCutoff, postSnapshotPush,
                  pushDuringClearing, releaseSawData>>

ReaderArmPending ==
  /\ readerPc = "Claimed"
  /\ consumerOwner = "Reader"
  /\ Len(queue) = 0
  /\ readerPc' = "Waiting"
  /\ consumerOwner' = "None"
  /\ portState' = "PENDING"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, producerPc, clearPc, clearSnapshot,
                  clearCutoff, postSnapshotPush, pushDuringClearing,
                  releaseSawData>>

ClearClaim ==
  /\ clearPc = "Ready"
  /\ consumerOwner = "None"
  /\ portState \in {"IDLE", "EVENT"}
  /\ clearPc' = "Claimed"
  /\ consumerOwner' = "Clear"
  /\ portState' = "CLEARING"
  /\ clearJustReleased' = FALSE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, producerPc, readerPc, clearSnapshot,
                  clearCutoff, postSnapshotPush, pushDuringClearing,
                  releaseSawData>>

ClearTakeSnapshot ==
  /\ clearPc = "Claimed"
  /\ consumerOwner = "Clear"
  /\ portState = "CLEARING"
  /\ clearPc' = IF queue = <<>> THEN "EmptyObserved" ELSE "Snapshotted"
  /\ clearSnapshot' = queue
  /\ clearCutoff' = Len(produced)
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, producerPc, readerPc, consumerOwner, portState,
                  postSnapshotPush, pushDuringClearing, releaseSawData,
                  clearJustReleased>>

ClearReleaseEmpty ==
  /\ clearPc = "EmptyObserved"
  /\ consumerOwner = "Clear"
  /\ portState = "CLEARING"
  /\ clearSnapshot = <<>>
  /\ clearPc' = "Done"
  /\ consumerOwner' = "None"
  /\ portState' = "IDLE"
  /\ clearJustReleased' = TRUE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, producerPc, readerPc, clearSnapshot,
                  clearCutoff, postSnapshotPush, pushDuringClearing,
                  releaseSawData>>

ClearDrop ==
  /\ clearPc = "Snapshotted"
  /\ consumerOwner = "Clear"
  /\ portState = "CLEARING"
  /\ IsPrefix(clearSnapshot, queue)
  /\ LET count == IF MovingClearBoundary THEN Len(queue)
                    ELSE Len(clearSnapshot)
         removed == TakePrefix(queue, count)
     IN /\ queue' = DropPrefix(queue, count)
        /\ removals' = removals \o removed
        /\ clearRemoved' = removed
  /\ clearPc' = "Dropped"
  /\ UNCHANGED <<produced, delivered, nextItem, producerPc, readerPc,
                  consumerOwner, portState, clearSnapshot, clearCutoff,
                  postSnapshotPush, pushDuringClearing, releaseSawData,
                  clearJustReleased>>

ClearObserveRelease ==
  /\ clearPc = "Dropped"
  /\ consumerOwner = "Clear"
  /\ portState = "CLEARING"
  /\ clearPc' = "ReleaseObserved"
  /\ releaseSawData' = (Len(queue) > 0)
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, producerPc, readerPc, consumerOwner, portState,
                  clearSnapshot, clearCutoff, postSnapshotPush,
                  pushDuringClearing, clearJustReleased>>

ClearStoreRelease ==
  /\ clearPc = "ReleaseObserved"
  /\ consumerOwner = "Clear"
  /\ portState = "CLEARING"
  /\ clearPc' = "Done"
  /\ consumerOwner' = "None"
  /\ portState' = IF releaseSawData THEN "EVENT" ELSE "IDLE"
  /\ clearJustReleased' = TRUE
  /\ UNCHANGED <<queue, produced, removals, delivered, clearRemoved,
                  nextItem, producerPc, readerPc, clearSnapshot,
                  clearCutoff, postSnapshotPush, pushDuringClearing,
                  releaseSawData>>

Next ==
  \/ ProducerPush
  \/ ProducerFinish
  \/ ProducerClaimPending
  \/ ProducerCompletePending
  \/ ProducerPublishEvent
  \/ ProducerNotificationDeferred
  \/ ReaderClaim
  \/ ReaderConsume
  \/ ReaderArmPending
  \/ ClearClaim
  \/ ClearTakeSnapshot
  \/ ClearReleaseEmpty
  \/ ClearDrop
  \/ ClearObserveRelease
  \/ ClearStoreRelease

Spec == Init /\ [][Next]_vars

FairSpec ==
  /\ Spec
  /\ WF_vars(ProducerClaimPending \/ ProducerPublishEvent \/
              ProducerNotificationDeferred)
  /\ WF_vars(ProducerCompletePending)
  /\ WF_vars(ReaderConsume \/ ReaderArmPending)
  /\ WF_vars(ClearTakeSnapshot)
  /\ WF_vars(ClearReleaseEmpty)
  /\ WF_vars(ClearDrop)
  /\ WF_vars(ClearObserveRelease)
  /\ WF_vars(ClearStoreRelease)

TypeOK ==
  /\ queue \in Seq(Items)
  /\ produced \in Seq(Items)
  /\ removals \in Seq(Items)
  /\ delivered \in Seq(Items)
  /\ clearRemoved \in Seq(Items)
  /\ nextItem \in 1..(MaxItems + 1)
  /\ producerPc \in ProducerPcs
  /\ readerPc \in ReaderPcs
  /\ clearPc \in ClearPcs
  /\ consumerOwner \in Owners
  /\ portState \in PortStates
  /\ clearSnapshot \in Seq(Items)
  /\ clearCutoff \in 0..MaxItems
  /\ postSnapshotPush \in BOOLEAN
  /\ pushDuringClearing \in BOOLEAN
  /\ releaseSawData \in BOOLEAN
  /\ clearJustReleased \in BOOLEAN

ProducedInOrder ==
  /\ nextItem = Len(produced) + 1
  /\ \A index \in 1..Len(produced) : produced[index] = index

QueueOrderPreserved == removals \o queue = produced

RemovalAccounting ==
  /\ SeqSet(removals) = SeqSet(delivered) \union SeqSet(clearRemoved)
  /\ SeqSet(delivered) \intersect SeqSet(clearRemoved) = {}
  /\ Len(removals) = Len(delivered) + Len(clearRemoved)

SingleConsumer ==
  /\ (consumerOwner = "Reader" <=> readerPc = "Claimed")
  /\ (consumerOwner = "Clear" <=>
       clearPc \in {"Claimed", "EmptyObserved", "Snapshotted", "Dropped",
                    "ReleaseObserved"})
  /\ (consumerOwner = "Pending" <=> producerPc = "Completing")
  /\ (consumerOwner = "None" <=>
       /\ readerPc # "Claimed"
       /\ clearPc \notin {"Claimed", "EmptyObserved", "Snapshotted", "Dropped",
                           "ReleaseObserved"}
       /\ producerPc # "Completing")

ClearSnapshotRemainsPrefix ==
  clearPc \notin {"EmptyObserved", "Snapshotted"} \/
    IsPrefix(clearSnapshot, queue)

NoPostSnapshotLoss ==
  /\ ~HasItemAfter(clearRemoved, clearCutoff)
  /\ clearPc \notin {"Dropped", "ReleaseObserved", "Done"} \/
       SeqSet(clearRemoved) \subseteq SeqSet(clearSnapshot)

ClearBoundaryIsFixed ==
  clearPc \notin {"Dropped", "ReleaseObserved", "Done"} \/
    clearRemoved = clearSnapshot

PostSnapshotDataIsPreserved ==
  ~(clearJustReleased /\ postSnapshotPush) \/
    HasItemAfter(queue, clearCutoff)

ClearReleaseMatchesObservation ==
  ~clearJustReleased \/
    portState = (IF releaseSawData THEN "EVENT" ELSE "IDLE")

ClearReleaseStateCorrect ==
  ~clearJustReleased \/
    IF Len(queue) > 0 THEN portState = "EVENT" ELSE portState = "IDLE"

PostSnapshotDataReturnsEvent ==
  ~(clearJustReleased /\ postSnapshotPush) \/ portState = "EVENT"

Safety ==
  /\ TypeOK
  /\ ProducedInOrder
  /\ NoDuplicates(produced)
  /\ QueueOrderPreserved
  /\ RemovalAccounting
  /\ SingleConsumer
  /\ ClearSnapshotRemainsPrefix
  /\ NoPostSnapshotLoss
  /\ ClearBoundaryIsFixed
  /\ PostSnapshotDataIsPreserved
  /\ ClearReleaseMatchesObservation

ProducerNotificationFinishes ==
  producerPc = "Notify" ~> producerPc = "Ready"

PendingCompletionFinishes ==
  producerPc = "Completing" ~> producerPc = "Ready"

ClaimedReaderAdvances ==
  readerPc = "Claimed" ~> readerPc \in {"Waiting", "Done"}

ClaimedClearFinishes ==
  clearPc \in {"Claimed", "EmptyObserved", "Snapshotted", "Dropped",
                "ReleaseObserved"} ~>
    clearPc = "Done"

ConsumerOwnerEventuallyReleases == consumerOwner # "None" ~> consumerOwner = "None"

=============================================================================
