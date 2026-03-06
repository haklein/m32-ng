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
  // Latches are updated from live lever_state in tick() — not here.
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

  // Update latches from live paddle state every tick — like the original
  // Morserino's checkPaddles().  Latches only accumulate (set, never cleared
  // here); they are cleared at element start below.
  if (lever_state == LEVER_DOT || lever_state == LEVER_DOT_DASH || lever_state == LEVER_DASH_DOT)
    dit_latch_ = true;
  if (lever_state == LEVER_DASH || lever_state == LEVER_DOT_DASH || lever_state == LEVER_DASH_DOT)
    dah_latch_ = true;

  if (!symbol_player.ready())
  {
    return;
  }

  // Element + inter-element gap finished.  Build effective lever state from
  // the accumulated latches (which reflect every paddle press/hold since the
  // last element started).
  LeverState effective;
  if (dit_latch_ && dah_latch_) {
    // Both paddles active — alternate from current element.
    if (keyer_state == KEYER_STATE_DOT || keyer_state == KEYER_STATE_ALTERNATING_DOT)
      effective = LEVER_DOT_DASH;    // was dit → next is dah
    else if (keyer_state == KEYER_STATE_DASH || keyer_state == KEYER_STATE_ALTERNATING_DASH)
      effective = LEVER_DASH_DOT;    // was dah → next is dit
    else
      effective = LEVER_DOT_DASH;    // from idle, dit-first
  } else if (dit_latch_) {
    effective = LEVER_DOT;
  } else if (dah_latch_) {
    effective = LEVER_DASH;
  } else {
    effective = LEVER_UNSET;
  }

  // Feed effective into nextKeyerState, then restore real lever_state.
  LeverState saved = lever_state;
  lever_state = effective;
  KeyerState next_keyer_state = nextKeyerState();
  lever_state = saved;

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

  // Clear latches at element start (like original Morserino clearPaddleLatches).
  // They will be re-set from live lever_state on subsequent ticks.
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