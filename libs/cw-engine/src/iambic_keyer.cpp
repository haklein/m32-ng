#include "iambic_keyer.h"

IambicKeyer::IambicKeyer(unsigned long duration_unit, play_state_changed_fun_ptr state_changed_cb, millis_fun_ptr millis_cb, bool mode_a) : symbol_player(duration_unit, state_changed_cb, millis_cb)
{
  this->millis_cb = millis_cb;
  this->mode_a = mode_a;
}

void IambicKeyer::setLeverState(LeverState lever_state)
{
  //printf("**  Lever state: %s -> %s\n", lever_state_str[this->lever_state], lever_state_str[lever_state]);
  if (this->lever_state != lever_state && (lever_state == LEVER_DOT_DASH || lever_state == LEVER_DASH_DOT))
  {
    // Enhanced Curtis B: only accept squeeze if past threshold % of element
    if (symbol_player.isPastElementThreshold(curtisb_dit_pct_, curtisb_dah_pct_)) {
      lever_upgrade = true;
    }
  }
  this->lever_state = lever_state;

  // Latch only the OPPOSITE paddle so brief taps during a playing element
  // aren't lost.  Same-paddle latching causes runaway repeats when the
  // paddle is released before the element finishes.
  bool is_dit_element = (keyer_state == KEYER_STATE_DOT || keyer_state == KEYER_STATE_ALTERNATING_DOT);
  bool is_dah_element = (keyer_state == KEYER_STATE_DASH || keyer_state == KEYER_STATE_ALTERNATING_DASH);
  if (!is_dit_element) {  // don't latch dit during a dit — only opposite
    if (lever_state == LEVER_DOT || lever_state == LEVER_DOT_DASH || lever_state == LEVER_DASH_DOT)
      dit_latch_ = true;
  }
  if (!is_dah_element) {  // don't latch dah during a dah — only opposite
    if (lever_state == LEVER_DASH || lever_state == LEVER_DOT_DASH || lever_state == LEVER_DASH_DOT)
      dah_latch_ = true;
  }
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
      if (prev_lever_state == LEVER_DOT_DASH || prev_lever_state == LEVER_DASH_DOT)
      {
        prev_lever_state = LEVER_UNSET;
        return KEYER_STATE_ALTERNATING_DASH;
      }
      else
      {
        return KEYER_STATE_STOPPED;
      }
    case LEVER_DOT:
      return KEYER_STATE_DOT;
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
      else
      {
        return KEYER_STATE_STOPPED;
      }
    case LEVER_DOT:
      return KEYER_STATE_DOT;
    case LEVER_DASH:
      return KEYER_STATE_DASH;
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
  {
    return;
  }

  // Merge paddle latches into lever_state so brief taps aren't lost.
  // This matches the original Morserino's latch-based keyer behaviour.
  LeverState effective = lever_state;
  if (dit_latch_ || dah_latch_) {
    bool dit_active = dit_latch_ || effective == LEVER_DOT
                      || effective == LEVER_DOT_DASH || effective == LEVER_DASH_DOT;
    bool dah_active = dah_latch_ || effective == LEVER_DASH
                      || effective == LEVER_DOT_DASH || effective == LEVER_DASH_DOT;
    if (dit_active && dah_active) {
      // Preserve squeeze direction from the last known lever state
      if (effective == LEVER_DOT || effective == LEVER_DOT_DASH)
        effective = LEVER_DOT_DASH;
      else if (effective == LEVER_DASH || effective == LEVER_DASH_DOT)
        effective = LEVER_DASH_DOT;
      else if (keyer_state == KEYER_STATE_DOT || keyer_state == KEYER_STATE_ALTERNATING_DOT)
        effective = LEVER_DOT_DASH;   // last element was dit → squeeze = dot-dash
      else
        effective = LEVER_DASH_DOT;
    } else if (dit_active) {
      effective = LEVER_DOT;
    } else {
      effective = LEVER_DASH;
    }
  }

  // Save effective as lever_state for nextKeyerState(), then clear latches.
  LeverState saved = lever_state;
  lever_state = effective;
  dit_latch_ = false;
  dah_latch_ = false;

  KeyerState next_keyer_state = nextKeyerState();

  lever_state = saved;  // restore real lever state

  if (!mode_a)
  {
    prev_lever_state = effective;
  }

  if (keyer_state == next_keyer_state && next_keyer_state == KEYER_STATE_STOPPED)
  {
    return;
  }

  // printf("= Keyer state: %s -> %s\n", keyer_state_str[keyer_state], keyer_state_str[next_keyer_state]);
  this->keyer_state = next_keyer_state;

  // Clear latches at the start of each new element (matches original Morserino).
  dit_latch_ = false;
  dah_latch_ = false;

  if (keyer_state == KEYER_STATE_DOT || keyer_state == KEYER_STATE_ALTERNATING_DOT)
  {
    symbol_player.playDot();
  }
  else if (keyer_state == KEYER_STATE_DASH || keyer_state == KEYER_STATE_ALTERNATING_DASH)
  {
    symbol_player.playDash();
  }
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