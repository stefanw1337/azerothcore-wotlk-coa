local DUNGEON_CLEAR_PROBE = "Command 'dc status' does not exist"

ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", function(_, _, message)
    return message ~= nil and message:find(DUNGEON_CLEAR_PROBE, 1, true) ~= nil
end)
