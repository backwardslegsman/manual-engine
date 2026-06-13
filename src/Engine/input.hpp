#pragma once

#include <cstdint>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

namespace Engine {
    enum class Key {
        W,
        A,
        S,
        D,
        E,
        F,
        P,
        Delete,
        Home,
        Up,
        Down,
        Left,
        Right,
        Count,
    };

    enum class MouseButton {
        Left,
        Middle,
        Right,
        Count,
    };

    // Per-frame input snapshot. App code feeds SDL events into this object;
    // engine systems query stable state from it.
    class InputState {
    public:
        void beginFrame();
        void processEvent(const SDL_Event& event);
        void setViewportSize(int width, int height);

        bool isKeyDown(Key key) const;
        bool wasKeyPressed(Key key) const;
        bool wasKeyReleased(Key key) const;
        bool isMouseButtonDown(MouseButton button) const;
        bool wasMouseButtonPressed(MouseButton button) const;
        bool wasMouseButtonReleased(MouseButton button) const;
        glm::vec2 mousePosition() const;
        glm::vec2 mouseDelta() const;
        glm::vec2 mouseWheel() const;
        glm::ivec2 viewportSize() const;

    private:
        static constexpr uint32_t keyCount = static_cast<uint32_t>(Key::Count);
        static constexpr uint32_t mouseButtonCount = static_cast<uint32_t>(MouseButton::Count);

        bool keys_[keyCount]{};
        bool keyPressed_[keyCount]{};
        bool keyReleased_[keyCount]{};
        bool mouseButtons_[mouseButtonCount]{};
        bool mouseButtonPressed_[mouseButtonCount]{};
        bool mouseButtonReleased_[mouseButtonCount]{};
        glm::vec2 mousePosition_{};
        glm::vec2 mouseDelta_{};
        glm::vec2 mouseWheel_{};
        glm::ivec2 viewportSize_{1280, 720};
    };
}
