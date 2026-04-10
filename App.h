#pragma once

class App {
public:
    virtual ~App() {}
    virtual const char* name() const = 0;
    virtual void setup() = 0;
    virtual void loop() = 0;
};
