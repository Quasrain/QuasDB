#pragma once

#include "dbformat.h"
#include "kv/include/db.h"

namespace QuasDB
{

  class SnapshotList;

  // Snapshots are kept in a doubly-linked list in the DB.
  // Each SnapshotImpl corresponds to a particular sequence number.
  class SnapshotImpl : public Snapshot
  {
  public:
    SnapshotImpl(SequenceNumber sequence_number)
        : sequence_number_(sequence_number) {}

    SequenceNumber sequence_number() const { return sequence_number_; }

  private:
    friend class SnapshotList;

    // SnapshotImpl is kept in a doubly-linked circular list. The SnapshotList
    // implementation operates on the next/previous fields direcly.
    SnapshotImpl *prev_;
    SnapshotImpl *next_;

    const SequenceNumber sequence_number_;

    SnapshotList *list_ = nullptr;
  };

  class SnapshotList
  {
  public:
    SnapshotList() : head_(0)
    {
      head_.prev_ = &head_;
      head_.next_ = &head_;
    }

    bool empty() const { return head_.next_ == &head_; }
    SnapshotImpl *oldest() const
    {
      assert(!empty());
      return head_.next_;
    }
    SnapshotImpl *newest() const
    {
      assert(!empty());
      return head_.prev_;
    }

    // Creates a SnapshotImpl and appends it to the end of the list.
    SnapshotImpl *New(SequenceNumber sequence_number)
    {
      assert(empty() || newest()->sequence_number_ <= sequence_number);

      SnapshotImpl *snapshot = new SnapshotImpl(sequence_number);

      snapshot->list_ = this;
      snapshot->next_ = &head_;
      snapshot->prev_ = head_.prev_;
      snapshot->prev_->next_ = snapshot;
      snapshot->next_->prev_ = snapshot;
      return snapshot;
    }

    // Removes a SnapshotImpl from this list.
    //
    // The snapshot must have been created by calling New() on this list.
    //
    // The snapshot pointer should not be const, because its memory is
    // deallocated. However, that would force us to change DB::ReleaseSnapshot(),
    // which is in the API, and currently takes a const Snapshot.
    void Delete(const SnapshotImpl *snapshot)
    {
      assert(snapshot->list_ == this);
      snapshot->prev_->next_ = snapshot->next_;
      snapshot->next_->prev_ = snapshot->prev_;
      delete snapshot;
    }

  private:
    // Dummy head of doubly-linked list of snapshots
    SnapshotImpl head_;
  };

}