#include "Engine/EventQueue.hpp"

namespace Engine {
    void EventQueue::publish(const InputActionEvent& event)
    {
        inputActions_.push_back(event);
    }

    void EventQueue::publish(const InteractionEvent& event)
    {
        interactionEvents_.push_back(event);
    }

    const std::vector<InputActionEvent>& EventQueue::inputActions() const
    {
        return inputActions_;
    }

    const std::vector<InteractionEvent>& EventQueue::interactionEvents() const
    {
        return interactionEvents_;
    }

    void EventQueue::clear()
    {
        inputActions_.clear();
        interactionEvents_.clear();
    }
}
