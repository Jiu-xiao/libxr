#include <array>

#include "libxr.hpp"
#include "test.hpp"

namespace
{
struct CallbackProbe
{
  LibXR::Callback<int> cb;
  LibXR::CallbackGuard<LibXR::Callback<int>> guard;
  bool runtime_in_isr = true;
  bool trigger_reentry = true;
  std::array<int, 4> seen = {};
  std::array<bool, 4> seen_in_isr = {};
  int seen_count = 0;
  int depth = 0;
  int max_depth = 0;

  CallbackProbe() : cb(LibXR::Callback<int>::Create(OnCallback, this)) {}

  static void OnCallback(bool in_isr, CallbackProbe* self, int value)
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
        self->guard.Run(self->cb, true, 2);
      }
      else
      {
        self->guard.Run(self->cb, false, 2);
      }
    }

    self->depth--;
  }
};

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
}  // namespace

void test_cb()
{
  {
    CallbackProbe probe;
    probe.runtime_in_isr = true;
    probe.guard.Run(probe.cb, probe.runtime_in_isr, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == true);
    ASSERT(probe.seen_in_isr[1] == true);
    ASSERT(probe.max_depth == 1);
  }

  {
    CallbackProbe probe;
    probe.runtime_in_isr = false;
    probe.guard.Run(probe.cb, probe.runtime_in_isr, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 1);
  }

  {
    CallbackProbe probe;
    probe.runtime_in_isr = true;
    probe.guard.Run(probe.cb, true, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == true);
    ASSERT(probe.seen_in_isr[1] == true);
    ASSERT(probe.max_depth == 1);
  }

  {
    CallbackProbe probe;
    probe.runtime_in_isr = false;
    probe.guard.Run(probe.cb, false, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 1);
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
}
