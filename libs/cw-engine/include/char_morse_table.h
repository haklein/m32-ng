#pragma once

// Consolidated char-to-Morse lookup.
// Previously duplicated in MorsePlayer.cpp and MorseTrainer.cpp; lives here once.
// Returns a string of '.' and '-' characters, ' ' for word space, "" for delimiters.
// No Arduino or platform dependency.

#include <string>

// Returns the Morse pattern for a single character (upper- or lower-case).
// Prosign delimiters '<' and '>' return ""; unknown characters return " ".
std::string char_to_morse(char c);
