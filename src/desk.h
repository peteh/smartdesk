#pragma once
#include <Arduino.h>
class Desk
{
public:
    Desk(const uint8_t outputUp, const uint8_t outputDown)
        : m_outputUp(outputUp),
          m_outputDown(outputDown)
    {
    }

    void begin();
    void stop();
    void moveUp();
    void moveDown();
    bool controlLoop(const double sensor, const double target);

private:
    const uint8_t m_outputUp;
    const uint8_t m_outputDown;

    double m_targetAccuracyCm = 5.;

    bool g_outputUp = false;
    bool g_outputDown = false;
};
