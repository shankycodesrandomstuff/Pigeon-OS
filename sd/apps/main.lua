local led_pin = 2
local state = 0

function init()
    print("Pigeon OS app init")
    gpio.write(led_pin, 0)
end

function update()
    state = 1 - state
    gpio.write(led_pin, state)
    print("blink", state)
    delay(500)
end

function on_event(event)
    if event and event.type then
        print("event:", event.type)
    end
end
