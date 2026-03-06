#include "iambic_keyer.h"

IambicKeyer::IambicKeyer(unsigned long duration_unit, play_state_changed_fun_ptr state_changed_cb, millis_fun_ptr millis_cb, bool mode_a) : symbol_player(duration_unit, state_changed_cb, millis_cb)
{
  this->millis_cb = millis_cb;
  this->mode_a = mode_a;
}

void IambicKeyer::setLeverState(LeverState lever_state)
{
  // Capture any new squeeze during an element so brief opposite-paddle taps
  // (e.g. the dah in F = di di da di) aren't lost if released before element ends.
  if (this->lever_state != lever_state && (lever_state == LEVER_DOT_DASH || lever_state == LEVER_DASH_DOT))
  {
    lever_upgrade = true;
  }
  this->lever_state = lever_state;
}

KeyerState IambicKeyer::nextKeyerState()
{
  // prev_lever_state = LEVER_UNSET;
  switch (keyer_state)
  {
  case KEYER_STATE_UNSET:
  case KEYER_STATE_STOPPED:
  case KEYER_STATE_DOT:
  case KEYER_STATE_DASH:
    if ((keyer_state == KEYER_STATE_DOT || keyer_state == KEYER_STATE_DASH) && lever_upgrade)
    {
      lever_upgrade = false;
      return (keyer_state == KEYER_STATE_DOT) ? KEYER_STATE_ALTERNATING_DASH : KEYER_STATE_ALTERNATING_DOT;
    }

    switch (lever_state)
    {
    case LEVER_UNSET:
      return KEYER_STATE_STOPPED;
    case LEVER_DOT:
      return KEYER_STATE_DOT;
    case LEVER_DASH:
      return KEYER_STATE_DASH;
    case LEVER_DOT_DASH:
      prev_lever_state = LEVER_DOT_DASH;
      return KEYER_STATE_ALTERNATING_DASH;
    case LEVER_DASH_DOT:
      prev_lever_state = LEVER_DASH_DOT;
      return KEYER_STATE_ALTERNATING_DOT;
    default:
      return KEYER_STATE_DASH;
    }
  case KEYER_STATE_ALTERNATING_DOT:
    switch (lever_state)
    {
    case LEVER_UNSET:
      // Curtis B: both paddles released during alternation → one more element.
      if (prev_lever_state == LEVER_DOT_DASH || prev_lever_state == LEVER_DASH_DOT)
      {
        prev_lever_state = LEVER_UNSET;
        return KEYER_STATE_ALTERNATING_DASH;
      }
      return KEYER_STATE_STOPPED;
    case LEVER_DOT:
      // Same paddle still held, opposite released → stop alternation.
      // (Operator released dah during final dit — e.g. C = -.-.)
      return KEYER_STATE_STOPPED;
    case LEVER_DASH:
      return KEYER_STATE_DASH;
    case LEVER_DOT_DASH:
    case LEVER_DASH_DOT:
      return KEYER_STATE_ALTERNATING_DASH;
    default:
      return KEYER_STATE_ALTERNATING_DOT;
    }
  case KEYER_STATE_ALTERNATING_DASH:
    switch (lever_state)
    {
    case LEVER_UNSET:
      if (prev_lever_state == LEVER_DOT_DASH || prev_lever_state == LEVER_DASH_DOT)
      {
        prev_lever_state = LEVER_UNSET;
        return KEYER_STATE_ALTERNATING_DOT;
      }
      return KEYER_STATE_STOPPED;
    case LEVER_DASH:
      // Same paddle still held, opposite released → stop alternation.
      return KEYER_STATE_STOPPED;
    case LEVER_DOT:
      return KEYER_STATE_DOT;
    case LEVER_DOT_DASH:
    case LEVER_DASH_DOT:
      return KEYER_STATE_ALTERNATING_DOT;
    default:
      return KEYER_STATE_ALTERNATING_DASH;
    }
  }
}

void IambicKeyer::tick()
{
  symbol_player.tick();

  if (!symbol_player.ready())
    return;

  // Element + inter-element gap finished.  Decide next element from the
  // current live lever_state (+ lever_upgrade for Curtis B mid-element squeeze).
  KeyerState next_keyer_state = nextKeyerState();

  if (!mode_a)
    prev_lever_state = lever_state;

  if (keyer_state == next_keyer_state && next_keyer_state == KEYER_STATE_STOPPED)
    return;

  this->keyer_state = next_keyer_state;

  if (keyer_state == KEYER_STATE_DOT || keyer_state == KEYER_STATE_ALTERNATING_DOT)
    symbol_player.playDot();
  else if (keyer_state == KEYER_STATE_DASH || keyer_state == KEYER_STATE_ALTERNATING_DASH)
    symbol_player.playDash();
}

void IambicKeyer::setDurationUnit(unsigned long duration_unit)
{
  symbol_player.setDurationUnit(duration_unit);
}

void IambicKeyer::setSpeedWPM(unsigned long speed_wpm)
{
  unsigned long duration_unit = 1000 * 60 / (50 * speed_wpm);
  setDurationUnit(duration_unit);
}

void IambicKeyer::setModeA(bool mode_a)
{
  this->mode_a = mode_a;
}

bool IambicKeyer::getModeA()
{
  return mode_a;
}

void IambicKeyer::setReleaseCompensation(unsigned long ms)
{
  symbol_player.setReleaseCompensation(ms);
}

void IambicKeyer::setCurtisBThreshold(uint8_t dit_pct, uint8_t dah_pct)
{
  curtisb_dit_pct_ = dit_pct;
  curtisb_dah_pct_ = dah_pct;
}