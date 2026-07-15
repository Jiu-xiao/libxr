#include "event.hpp"

using namespace LibXR;

template class LibXR::Callback<uint32_t>;

namespace
{

int CompareEventId(uint32_t a, uint32_t b)
{
  if (a < b)
  {
    return -1;
  }
  if (a > b)
  {
    return 1;
  }
  return 0;
}

}  // namespace

Event::Event()
    : rbt_([](const uint32_t& a, const uint32_t& b) { return CompareEventId(a, b); })
{
}

void Event::Register(uint32_t event, const Callback& cb)
{
  auto list = rbt_.Search<LockFreeList>(event);

  if (!list)
  {
    list = new RBTree<uint32_t>::Node<LockFreeList>;
    rbt_.Insert(*list, event);
  }

  LockFreeList::Node<Block>* node = new LockFreeList::Node<Block>;

  node->data_.event = event;
  node->data_.cb = cb;
  list->data_.Add(*node);
}

void Event::Active(uint32_t event)
{
  auto list = rbt_.Search<LockFreeList>(event);
  if (!list)
  {
    return;
  }

  auto foreach_fun = [=](Block& block)
  {
    block.cb.Run(false, event);
    return ErrorCode::OK;
  };

  list->data_.Foreach<LibXR::Event::Block>(foreach_fun);
}

void Event::ActiveFromCallback(CallbackList list, uint32_t event, bool in_isr)
{
  if (!list)
  {
    return;
  }

  auto foreach_fun = [=](Block& block)
  {
    block.cb.Run(in_isr, event);
    return ErrorCode::OK;
  };

  list->Foreach<Block>(foreach_fun);
}

Event::CallbackList Event::GetList(uint32_t event)
{
  auto node = rbt_.Search<LockFreeList>(event);
  if (!node)
  {
    auto list = new RBTree<uint32_t>::Node<LockFreeList>;
    rbt_.Insert(*list, event);
    node = list;
  }
  return &node->data_;
}

void Event::Bind(Event& sources, uint32_t source_event, uint32_t target_event)
{
  struct BindBlock
  {
    Event* target;
    Event::CallbackList list;
    uint32_t event;
  };

  auto block = new BindBlock{this, GetList(target_event), target_event};

  auto bind_fun = [](bool in_isr, BindBlock* block, uint32_t event)
  {
    UNUSED(event);
    block->target->ActiveFromCallback(block->list, block->event, in_isr);
  };

  auto cb = Callback::Create(bind_fun, block);

  sources.Register(source_event, cb);
}
