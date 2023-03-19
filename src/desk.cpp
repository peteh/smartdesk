#include <esplog.h>
#include "desk.h"

void Desk::begin()
{
  pinMode(m_outputUp, OUTPUT);
  pinMode(m_outputDown, OUTPUT);

  digitalWrite(m_outputUp, g_outputUp);
  digitalWrite(m_outputDown, g_outputUp);
}



void Desk::moveUp()
{
  g_outputUp = true;
  g_outputDown = false;
  digitalWrite(m_outputUp, g_outputUp);
  digitalWrite(m_outputDown, g_outputDown);
}

void Desk::stop()
{
  log_info("Desk Stop!");
  g_outputUp = false;
  g_outputDown = false;
  digitalWrite(m_outputUp, g_outputUp);
  digitalWrite(m_outputDown, g_outputDown);
}

void Desk::moveDown()
{
  g_outputUp = false;
  g_outputDown = true;
  digitalWrite(m_outputUp, g_outputUp);
  digitalWrite(m_outputDown, g_outputDown);
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