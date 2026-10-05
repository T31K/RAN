// Refreshing a user's game copy ("RanOdyssey Native/game") from a newer app bundle.
// The package script stamps the bundled game with .data-version (a hash of its data). When the
// stamp differs from the copy's, the game-content folders are re-cloned from the bundle; the
// player's own files at the top level (option.ini, cache, ...) are left alone.
#pragma once
#include <string>

namespace ran_platform {

// True when the bundle has a .data-version stamp that the user's copy does not match.
bool GameDataStale(const std::string& bundledGame, const std::string& userGame);

// Replaces data/, textures/ and sounds/ in the user's copy with clones of the bundle's (each
// swapped in whole, so an interrupted sync leaves the old folder in place), then copies the
// stamp. Returns false if any folder could not be replaced.
bool SyncGameData(const std::string& bundledGame, const std::string& userGame);

} // namespace ran_platform
