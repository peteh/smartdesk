#pragma once
#include <Arduino.h>
class Desk
{
public:
    Desk(const uint8_t outputUp, const uint8_t outputDown, const uint16_t minHeight, const uint16_t maxHeight)
        : m_outputUp(outputUp),
          m_outputDown(outputDown),
          m_minHeight(minHeight),
          m_maxHeight(maxHeight)
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

    void setMinHeight(uint16_t minHeight)
    {
        m_minHeight = minHeight;
    }

    void setMaxHeight(uint16_t maxHeight)
    {
        m_maxHeight = maxHeight;
    }

private:
    const uint8_t m_outputUp;
    const uint8_t m_outputDown;
    uint16_t m_minHeight;
    uint16_t m_maxHeight;

    double m_targetAccuracyCm = 5.;

    bool m_up = false;
    bool m_down = false;

    long m_lastStop = 0;

    const long SWITCH_DELAY_MS = 4000;
};
