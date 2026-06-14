#include "Engine/InputMapping.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace {
    std::string normalizeName(std::string value)
    {
        value.erase(
            std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                return c == '_' || c == '-' || std::isspace(c);
            }),
            value.end()
        );
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool parseKey(const std::string& name, Engine::Key& key)
    {
        const std::string normalized = normalizeName(name);
        if (normalized == "w") {
            key = Engine::Key::W;
        } else if (normalized == "a") {
            key = Engine::Key::A;
        } else if (normalized == "s") {
            key = Engine::Key::S;
        } else if (normalized == "d") {
            key = Engine::Key::D;
        } else if (normalized == "e") {
            key = Engine::Key::E;
        } else if (normalized == "f") {
            key = Engine::Key::F;
        } else if (normalized == "p") {
            key = Engine::Key::P;
        } else if (normalized == "delete" || normalized == "del") {
            key = Engine::Key::Delete;
        } else if (normalized == "home") {
            key = Engine::Key::Home;
        } else if (normalized == "escape" || normalized == "esc") {
            key = Engine::Key::Escape;
        } else if (normalized == "space" || normalized == "spacebar") {
            key = Engine::Key::Space;
        } else if (normalized == "up") {
            key = Engine::Key::Up;
        } else if (normalized == "down") {
            key = Engine::Key::Down;
        } else if (normalized == "left") {
            key = Engine::Key::Left;
        } else if (normalized == "right") {
            key = Engine::Key::Right;
        } else {
            return false;
        }
        return true;
    }

    bool parseMouseButton(const std::string& name, Engine::MouseButton& button)
    {
        const std::string normalized = normalizeName(name);
        if (normalized == "left") {
            button = Engine::MouseButton::Left;
        } else if (normalized == "middle" || normalized == "mmb") {
            button = Engine::MouseButton::Middle;
        } else if (normalized == "right") {
            button = Engine::MouseButton::Right;
        } else {
            return false;
        }
        return true;
    }

    std::vector<Engine::Key> parseKeyList(const YAML::Node& node)
    {
        std::vector<Engine::Key> keys;
        if (!node || !node.IsSequence()) {
            return keys;
        }

        for (const YAML::Node& item : node) {
            Engine::Key key;
            if (parseKey(item.as<std::string>(), key)) {
                keys.push_back(key);
            }
        }
        return keys;
    }

    bool anyKeyDown(const Engine::InputState& input, const std::vector<Engine::Key>& keys)
    {
        return std::any_of(keys.begin(), keys.end(), [&](Engine::Key key) {
            return input.isKeyDown(key);
        });
    }

    bool anyKeyPressed(const Engine::InputState& input, const std::vector<Engine::Key>& keys)
    {
        return std::any_of(keys.begin(), keys.end(), [&](Engine::Key key) {
            return input.wasKeyPressed(key);
        });
    }

    bool anyKeyReleased(const Engine::InputState& input, const std::vector<Engine::Key>& keys)
    {
        return std::any_of(keys.begin(), keys.end(), [&](Engine::Key key) {
            return input.wasKeyReleased(key);
        });
    }

    glm::vec2 parseVec2(const YAML::Node& node, const glm::vec2& fallback)
    {
        if (!node || !node.IsSequence() || node.size() != 2) {
            return fallback;
        }
        return {node[0].as<float>(), node[1].as<float>()};
    }
}

namespace Engine {
    InputMapping InputMapping::defaultCameraMapping()
    {
        InputMapping mapping;

        Axis2Action pan;
        pan.action = "camera.pan";
        pan.mouseDrag = MouseDragAxis{MouseButton::Right, {1.0f, 1.0f}};
        pan.edgeScroll = true;
        mapping.axis2Actions_.push_back(pan);

        Axis2Action playerMove;
        playerMove.action = "player.move";
        playerMove.keys.positiveY = {Key::W};
        playerMove.keys.negativeY = {Key::S};
        playerMove.keys.positiveX = {Key::D};
        playerMove.keys.negativeX = {Key::A};
        mapping.axis2Actions_.push_back(playerMove);

        Axis2Action rotate;
        rotate.action = "camera.rotate";
        rotate.mouseDrag = MouseDragAxis{MouseButton::Middle, {1.0f, -1.0f}};
        mapping.axis2Actions_.push_back(rotate);

        mapping.scalarActions_.push_back({"camera.zoom", -1.0f});
        mapping.digitalActions_.push_back({"camera.toggle_follow", {Key::F}, {}});
        mapping.digitalActions_.push_back({"camera.recenter", {Key::Home}, {}});
        mapping.digitalActions_.push_back({"player.set_destination", {}, {MouseButton::Right}});
        mapping.digitalActions_.push_back({"player.cancel_destination", {Key::Escape}, {}});
        mapping.digitalActions_.push_back({"player.stop", {Key::Space}, {}});
        mapping.digitalActions_.push_back({"interaction.select", {}, {MouseButton::Left}});
        mapping.digitalActions_.push_back({"interaction.interact", {Key::E}, {}});
        mapping.digitalActions_.push_back({"interaction.remove_object", {Key::Delete}, {}});
        mapping.digitalActions_.push_back({"interaction.place_marker", {Key::P}, {}});
        return mapping;
    }

    InputMappingLoadResult InputMapping::loadFromYaml(const std::filesystem::path& path)
    {
        try {
            const YAML::Node root = YAML::LoadFile(path.string());
            const YAML::Node actions = root["input"]["actions"];
            if (!actions || !actions.IsMap()) {
                return {false, "input.actions must be a YAML map.", defaultCameraMapping()};
            }

            InputMapping mapping;
            for (const auto& actionNode : actions) {
                const std::string actionName = actionNode.first.as<std::string>();
                const YAML::Node actionConfig = actionNode.second;

                if (const YAML::Node axis2 = actionConfig["axis2"]) {
                    Axis2Action action;
                    action.action = actionName;

                    if (const YAML::Node keys = axis2["keys"]) {
                        action.keys.positiveX = parseKeyList(keys["positive_x"]);
                        action.keys.negativeX = parseKeyList(keys["negative_x"]);
                        action.keys.positiveY = parseKeyList(keys["positive_y"]);
                        action.keys.negativeY = parseKeyList(keys["negative_y"]);
                    }

                    if (const YAML::Node drag = axis2["mouse_drag"]) {
                        MouseButton button;
                        if (drag["button"] && parseMouseButton(drag["button"].as<std::string>(), button)) {
                            action.mouseDrag = MouseDragAxis{
                                button,
                                parseVec2(drag["scale"], {1.0f, 1.0f}),
                            };
                        }
                    }

                    action.edgeScroll = axis2["edge_scroll"] ? axis2["edge_scroll"].as<bool>() : false;
                    action.edgeScrollMarginPixels = axis2["edge_scroll_margin_pixels"]
                        ? axis2["edge_scroll_margin_pixels"].as<int>()
                        : 18;
                    mapping.axis2Actions_.push_back(action);
                }

                if (const YAML::Node scalar = actionConfig["scalar"]) {
                    ScalarAction action;
                    action.action = actionName;
                    action.mouseWheelYScale = scalar["mouse_wheel_y"] ? scalar["mouse_wheel_y"].as<float>() : 0.0f;
                    mapping.scalarActions_.push_back(action);
                }

                if (const YAML::Node digital = actionConfig["digital"]) {
                    DigitalAction action;
                    action.action = actionName;
                    action.keys = parseKeyList(digital["keys"]);
                    if (const YAML::Node buttons = digital["mouse_buttons"]; buttons && buttons.IsSequence()) {
                        for (const YAML::Node& buttonNode : buttons) {
                            MouseButton button;
                            if (parseMouseButton(buttonNode.as<std::string>(), button)) {
                                action.mouseButtons.push_back(button);
                            }
                        }
                    }
                    mapping.digitalActions_.push_back(action);
                }
            }

            return {true, {}, mapping};
        } catch (const std::exception& error) {
            std::ostringstream message;
            message << "Failed to load input mapping '" << path.string() << "': " << error.what();
            return {false, message.str(), defaultCameraMapping()};
        }
    }

    void InputMapping::publishEvents(const InputState& input, EventQueue& events) const
    {
        for (const Axis2Action& action : axis2Actions_) {
            glm::vec2 keyValue{};
            if (anyKeyDown(input, action.keys.positiveX)) {
                keyValue.x += 1.0f;
            }
            if (anyKeyDown(input, action.keys.negativeX)) {
                keyValue.x -= 1.0f;
            }
            if (anyKeyDown(input, action.keys.positiveY)) {
                keyValue.y += 1.0f;
            }
            if (anyKeyDown(input, action.keys.negativeY)) {
                keyValue.y -= 1.0f;
            }

            if (glm::dot(keyValue, keyValue) > 1.0f) {
                keyValue = glm::normalize(keyValue);
            }
            if (glm::dot(keyValue, keyValue) > 0.0f) {
                events.publish({
                    action.action,
                    InputActionPhase::Held,
                    InputActionPayloadType::Axis2,
                    InputActionSource::Keyboard,
                    false,
                    keyValue,
                    0.0f,
                });
            }

            if (action.mouseDrag && input.isMouseButtonDown(action.mouseDrag->button)) {
                const glm::vec2 value = input.mouseDelta() * action.mouseDrag->scale;
                if (glm::dot(value, value) > 0.0f) {
                    events.publish({
                        action.action,
                        InputActionPhase::Held,
                        InputActionPayloadType::Axis2,
                        InputActionSource::MouseDrag,
                        false,
                        value,
                        0.0f,
                    });
                }
            }

            if (action.edgeScroll) {
                glm::vec2 value{};
                const glm::ivec2 viewport = input.viewportSize();
                const glm::vec2 mousePosition = input.mousePosition();
                if (mousePosition.x <= static_cast<float>(action.edgeScrollMarginPixels)) {
                    value.x += 1.0f;
                }
                if (mousePosition.x >= static_cast<float>(viewport.x - action.edgeScrollMarginPixels)) {
                    value.x -= 1.0f;
                }
                if (mousePosition.y <= static_cast<float>(action.edgeScrollMarginPixels)) {
                    value.y += 1.0f;
                }
                if (mousePosition.y >= static_cast<float>(viewport.y - action.edgeScrollMarginPixels)) {
                    value.y -= 1.0f;
                }
                if (glm::dot(value, value) > 1.0f) {
                    value = glm::normalize(value);
                }
                if (glm::dot(value, value) > 0.0f) {
                    events.publish({
                        action.action,
                        InputActionPhase::Held,
                        InputActionPayloadType::Axis2,
                        InputActionSource::EdgeScroll,
                        false,
                        value,
                        0.0f,
                    });
                }
            }
        }

        for (const ScalarAction& action : scalarActions_) {
            const float value = input.mouseWheel().y * action.mouseWheelYScale;
            if (value != 0.0f) {
                events.publish({
                    action.action,
                    InputActionPhase::Held,
                    InputActionPayloadType::Scalar,
                    InputActionSource::MouseWheel,
                    false,
                    {},
                    value,
                });
            }
        }

        for (const DigitalAction& action : digitalActions_) {
            const bool pressed =
                anyKeyPressed(input, action.keys) ||
                std::any_of(action.mouseButtons.begin(), action.mouseButtons.end(), [&](MouseButton button) {
                    return input.wasMouseButtonPressed(button);
                });
            const bool released =
                anyKeyReleased(input, action.keys) ||
                std::any_of(action.mouseButtons.begin(), action.mouseButtons.end(), [&](MouseButton button) {
                    return input.wasMouseButtonReleased(button);
                });
            const bool held =
                anyKeyDown(input, action.keys) ||
                std::any_of(action.mouseButtons.begin(), action.mouseButtons.end(), [&](MouseButton button) {
                    return input.isMouseButtonDown(button);
                });

            const bool mousePressed = std::any_of(action.mouseButtons.begin(), action.mouseButtons.end(), [&](MouseButton button) {
                return input.wasMouseButtonPressed(button);
            });
            const bool mouseHeld = std::any_of(action.mouseButtons.begin(), action.mouseButtons.end(), [&](MouseButton button) {
                return input.isMouseButtonDown(button);
            });
            const bool mouseReleased = std::any_of(action.mouseButtons.begin(), action.mouseButtons.end(), [&](MouseButton button) {
                return input.wasMouseButtonReleased(button);
            });
            const InputActionSource source = mousePressed || mouseHeld || mouseReleased
                ? InputActionSource::MouseButton
                : InputActionSource::Keyboard;

            if (pressed) {
                events.publish({action.action, InputActionPhase::Pressed, InputActionPayloadType::Digital, source, true});
            }
            if (held) {
                events.publish({action.action, InputActionPhase::Held, InputActionPayloadType::Digital, source, true});
            }
            if (released) {
                events.publish({action.action, InputActionPhase::Released, InputActionPayloadType::Digital, source, false});
            }
        }
    }
}
