local validation = editor.commands.validate({ require_saved_build = false })
editor.log(validation.valid and "Profile validation passed." or "Profile validation failed.")

local dirty = editor.dirty()
local count = 0
for _, _ in ipairs(dirty.domains) do
    count = count + 1
end
editor.log("Dirty domains: " .. tostring(count))
