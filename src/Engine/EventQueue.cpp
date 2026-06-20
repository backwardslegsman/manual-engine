#include "Engine/EventQueue.hpp"

namespace Engine {
    void EventQueue::publish(const InputActionEvent& event)
    {
        inputActions_.push_back(event);
    }

    const std::vector<InputActionEvent>& EventQueue::inputActions() const
    {
        return inputActions_;
    }

    void EventQueue::clear()
    {
        inputActions_.clear();
    }
}
