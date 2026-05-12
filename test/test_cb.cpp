#include <array>

#include "libxr.hpp"
#include "test.hpp"

namespace
{
struct DirectCallbackProbe
{
  LibXR::Callback<int> cb;
  bool runtime_in_isr = true;
  bool trigger_reentry = true;
  std::array<int, 4> seen = {};
  std::array<bool, 4> seen_in_isr = {};
  int seen_count = 0;
  int depth = 0;
  int max_depth = 0;

  DirectCallbackProbe() : cb(LibXR::Callback<int>::Create(OnCallback, this)) {}

  static void OnCallback(bool in_isr, DirectCallbackProbe* self, int value)
  {
    if (self->seen_count < static_cast<int>(self->seen.size()))
    {
      self->seen[static_cast<size_t>(self->seen_count++)] = value;
      self->seen_in_isr[static_cast<size_t>(self->seen_count - 1)] = in_isr;
    }

    self->depth++;
    if (self->depth > self->max_depth)
    {
      self->max_depth = self->depth;
    }

    if (self->trigger_reentry && value == 1)
    {
      self->trigger_reentry = false;
      if (self->runtime_in_isr)
      {
        self->cb.Run(true, 2);
      }
      else
      {
        self->cb.Run(false, 2);
      }
    }

    self->depth--;
  }
};

struct GuardedCreationProbe
{
  LibXR::Callback<int> cb;
  bool runtime_in_isr = true;
  bool trigger_reentry = true;
  std::array<int, 4> seen = {};
  std::array<bool, 4> seen_in_isr = {};
  int seen_count = 0;
  int depth = 0;
  int max_depth = 0;

  GuardedCreationProbe() : cb(LibXR::Callback<int>::CreateGuarded(OnCallback, this)) {}

  static void OnCallback(bool in_isr, GuardedCreationProbe* self, int value)
  {
    if (self->seen_count < static_cast<int>(self->seen.size()))
    {
      self->seen[static_cast<size_t>(self->seen_count++)] = value;
      self->seen_in_isr[static_cast<size_t>(self->seen_count - 1)] = in_isr;
    }

    self->depth++;
    if (self->depth > self->max_depth)
    {
      self->max_depth = self->depth;
    }

    if (self->trigger_reentry && value == 1)
    {
      self->trigger_reentry = false;
      self->cb.Run(in_isr, 2);
    }

    self->depth--;
  }
};

struct LambdaCreationProbe
{
  LibXR::Callback<int> cb;
  int seen_value = 0;
  bool seen_in_isr = false;

  LambdaCreationProbe()
      : cb(LibXR::Callback<int>::Create(
            [](bool in_isr, LambdaCreationProbe* self, int value)
            {
              self->seen_value = value;
              self->seen_in_isr = in_isr;
            },
            this))
  {
  }
};
}  // namespace

void test_cb()
{
  {
    LibXR::Callback<int> empty_cb;
    ASSERT(empty_cb.Empty());
    empty_cb.Run(false, 1);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = true;
    probe.cb.Run(probe.runtime_in_isr, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == true);
    ASSERT(probe.seen_in_isr[1] == true);
    ASSERT(probe.max_depth == 2);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = false;
    probe.cb.Run(probe.runtime_in_isr, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 2);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = true;
    probe.cb.Run(true, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == true);
    ASSERT(probe.seen_in_isr[1] == true);
    ASSERT(probe.max_depth == 2);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = false;
    probe.cb.Run(false, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 2);
  }

  {
    GuardedCreationProbe probe;
    probe.runtime_in_isr = false;
    probe.cb.Run(false, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 1);
  }

  {
    LambdaCreationProbe probe;
    probe.cb.Run(true, 7);
    ASSERT(probe.seen_value == 7);
    ASSERT(probe.seen_in_isr == true);
  }
}
