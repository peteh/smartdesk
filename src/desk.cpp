#include <esplog.h>
#include "desk.h"

void Desk::begin()
{
  pinMode(m_outputUp, OUTPUT);
  pinMode(m_outputDown, OUTPUT);

  digitalWrite(m_outputUp, m_up);
  digitalWrite(m_outputDown, m_up);
}

void Desk::moveUp()
{
  if(m_down)
  {
    stop();
  }

  if(millis() - m_lastStop > SWITCH_DELAY_MS && !m_up) 
  {
    log_info("Desk Up!");
    m_up = true;
    m_down = false;
    digitalWrite(m_outputUp, m_up);
    digitalWrite(m_outputDown, m_down);
  }
}

void Desk::stop()
{
  if(!m_up && !m_down)
  {
    return;
  }

  log_info("Desk Stop!");
  m_up = false;
  m_down = false;
  digitalWrite(m_outputUp, m_up);
  digitalWrite(m_outputDown, m_down);
  m_lastStop = millis();
}

void Desk::moveDown()
{
  if(m_up)
  {
    stop();
  }

  if(millis() - m_lastStop > SWITCH_DELAY_MS && !m_down) 
  {
    log_info("Desk Down!");
    m_up = false;
    m_down = true;
    digitalWrite(m_outputUp, m_up);
    digitalWrite(m_outputDown, m_down);
  }
}

bool Desk::controlLoop(const double sensorCm, const double targetCm)
{
  double distance = targetCm - sensorCm;
  if (abs(distance) < m_targetAccuracyCm)
  {
    log_info("Reached target position (target: %.2fcm, is: %.2f)", targetCm, sensorCm);
    stop();
    return true;
  }
  else if (distance > 0) // need to go up
  {
    moveUp();
  }
  else if (distance < 0) // need to go down
  {
    moveDown();
  }
  return false;
}


bool Desk::isMoving()
{
  return m_down || m_up;
}