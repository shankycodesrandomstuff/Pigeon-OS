#pragma once

class App {
public:
    virtual ~App() {}
    virtual void setup() = 0;
    virtual void loop() = 0;
};
