-- custom models: draw your own weapon model instead of the stock one, tint enemies, hide hats.
-- put the model files under tf\custom\<folder>\ (models\..., materials\...) and turn on the
-- sv_pure bypass checkbox for sv_pure servers. the model must be rigged to the stock skeleton.
local from_input = ui.input("weapon to replace", "c_scattergun")
local gun_input  = ui.input("weapon model", "")
local tint       = ui.checkbox("tint enemies", true)
local tint_color = ui.color("enemy color", 255, 80, 80, 255)
local no_hats    = ui.checkbox("hide hats", false)

-- paths only change on the button: every path botox loads stays loaded, half typed ones too
local gun_from, gun_to = from_input:get(), gun_input:get()
ui.button("apply models", function()
    gun_from, gun_to = from_input:get(), gun_input:get()
end)

-- draw_model runs hundreds of times a frame: read the ui once a frame here
local want_tint, enemy_color, hide_hats = false, nil, false
callbacks.register("paint", function()
    want_tint, enemy_color, hide_hats = tint:get(), color(tint_color:get()), no_hats:get()
end)

callbacks.register("draw_model", function(model)
    local name = model.name

    if gun_to ~= "" and gun_from ~= "" and name:find(gun_from, 1, true) then
        model.model = gun_to
    elseif hide_hats and name:find("models/player/items/", 1, true) then
        model.hide = true
    elseif want_tint then
        local player = model.entity
        if player and player:is_player() and player:is_enemy() then
            model.color = enemy_color
        end
    end
end)
