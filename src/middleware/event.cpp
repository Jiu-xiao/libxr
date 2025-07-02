#include "event.hpp"

using namespace LibXR;

template class LibXR::Callback<uint32_t>;

Event::Event()
    : rbt_([](const uint32_t &a, const uint32_t &b)
           { return static_cast<int>(a) - static_cast<int>(b); })
{
}

void Event::Register(uint32_t event, const Callback &cb)
{
  auto list = rbt_.Search<LockFreeList>(event);

  if (!list)
  {
    list = new RBTree<uint32_t>::Node<LockFreeList>;
    rbt_.Insert(*list, event);
  }

  LockFreeList::Node<Block> *node = new LockFreeList::Node<Block>;

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

  auto foreach_fun = [=](Block &block)
  {
    block.cb.Run(false, event);
    return ErrorCode::OK;
  };

  list->data_.Foreach<LibXR::Event::Block>(foreach_fun);
}

void Event::ActiveFromCallback(CallbackList list, uint32_t event)
{
  if (!list)
  {
    return;
  }

  auto foreach_fun = [=](Block &block)
  {
    block.cb.Run(true, event);
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

void Event::Bind(Event &sources, uint32_t source_event, uint32_t target_event)
{
  struct BindBlock
  {
    Event *target;
    uint32_t event;
  };

  auto block = new BindBlock{this, target_event};

  auto bind_fun = [](bool in_isr, BindBlock *block, uint32_t event)
  {
    UNUSED(event);
    UNUSED(in_isr);
    block->target->ActiveFromCallback(block->target->GetList(block->event), block->event);
  };

  auto cb = Callback::Create(bind_fun, block);

  sources.Register(source_event, cb);
}
