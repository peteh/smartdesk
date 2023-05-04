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
    bool isMoving();
    bool controlLoop(const double sensor, const double target);
    double getTargetAccuracyCm()
    {
        return m_targetAccuracyCm;
    }

private:
    const uint8_t m_outputUp;
    const uint8_t m_outputDown;

    double m_targetAccuracyCm = 5.;

    bool m_up = false;
    bool m_down = false;

    long m_lastStop = 0;

    const long SWITCH_DELAY_MS = 4000;
};
