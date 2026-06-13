#include "Engine/input.hpp"

#include <algorithm>

namespace {
    bool mapKey(SDL_Keycode keycode, Engine::Key& key)
    {
        switch (keycode) {
            case SDLK_W:
                key = Engine::Key::W;
                return true;
            case SDLK_A:
                key = Engine::Key::A;
                return true;
            case SDLK_S:
                key = Engine::Key::S;
                return true;
            case SDLK_D:
                key = Engine::Key::D;
                return true;
            case SDLK_UP:
                key = Engine::Key::Up;
                return true;
            case SDLK_DOWN:
                key = Engine::Key::Down;
                return true;
            case SDLK_LEFT:
                key = Engine::Key::Left;
                return true;
            case SDLK_RIGHT:
                key = Engine::Key::Right;
                return true;
            default:
                return false;
        }
    }

    bool mapMouseButton(uint8_t sdlButton, Engine::MouseButton& button)
    {
        switch (sdlButton) {
            case SDL_BUTTON_LEFT:
                button = Engine::MouseButton::Left;
                return true;
            case SDL_BUTTON_MIDDLE:
                button = Engine::MouseButton::Middle;
                return true;
            case SDL_BUTTON_RIGHT:
                button = Engine::MouseButton::Right;
                return true;
            default:
                return false;
        }
    }
}

namespace Engine {
    void InputState::beginFrame()
    {
        std::fill(std::begin(keyPressed_), std::end(keyPressed_), false);
        std::fill(std::begin(keyReleased_), std::end(keyReleased_), false);
        std::fill(std::begin(mouseButtonPressed_), std::end(mouseButtonPressed_), false);
        std::fill(std::begin(mouseButtonReleased_), std::end(mouseButtonReleased_), false);
        mouseDelta_ = {};
        mouseWheel_ = {};
    }

    void InputState::processEvent(const SDL_Event& event)
    {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                Key key;
                if (mapKey(event.key.key, key)) {
                    const uint32_t index = static_cast<uint32_t>(key);
                    const bool isDown = event.type == SDL_EVENT_KEY_DOWN;
                    if (isDown && !keys_[index]) {
                        keyPressed_[index] = true;
                    }
                    if (!isDown && keys_[index]) {
                        keyReleased_[index] = true;
                    }
                    keys_[index] = isDown;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                MouseButton button;
                if (mapMouseButton(event.button.button, button)) {
                    const uint32_t index = static_cast<uint32_t>(button);
                    const bool isDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if (isDown && !mouseButtons_[index]) {
                        mouseButtonPressed_[index] = true;
                    }
                    if (!isDown && mouseButtons_[index]) {
                        mouseButtonReleased_[index] = true;
                    }
                    mouseButtons_[index] = isDown;
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                mousePosition_ = {event.motion.x, event.motion.y};
                mouseDelta_ += glm::vec2{event.motion.xrel, event.motion.yrel};
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                mouseWheel_ += glm::vec2{event.wheel.x, event.wheel.y};
                break;
            default:
                break;
        }
    }

    void InputState::setViewportSize(int width, int height)
    {
        viewportSize_ = {width > 1 ? width : 1, height > 1 ? height : 1};
    }

    bool InputState::isKeyDown(Key key) const
    {
        return keys_[static_cast<uint32_t>(key)];
    }

    bool InputState::wasKeyPressed(Key key) const
    {
        return keyPressed_[static_cast<uint32_t>(key)];
    }

    bool InputState::wasKeyReleased(Key key) const
    {
        return keyReleased_[static_cast<uint32_t>(key)];
    }

    bool InputState::isMouseButtonDown(MouseButton button) const
    {
        return mouseButtons_[static_cast<uint32_t>(button)];
    }

    bool InputState::wasMouseButtonPressed(MouseButton button) const
    {
        return mouseButtonPressed_[static_cast<uint32_t>(button)];
    }

    bool InputState::wasMouseButtonReleased(MouseButton button) const
    {
        return mouseButtonReleased_[static_cast<uint32_t>(button)];
    }

    glm::vec2 InputState::mousePosition() const
    {
        return mousePosition_;
    }

    glm::vec2 InputState::mouseDelta() const
    {
        return mouseDelta_;
    }

    glm::vec2 InputState::mouseWheel() const
    {
        return mouseWheel_;
    }

    glm::ivec2 InputState::viewportSize() const
    {
        return viewportSize_;
    }
}
