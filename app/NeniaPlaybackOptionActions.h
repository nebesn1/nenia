#pragma once

#include <nenia/NeniaPlaylistController.h>
#include <nenia/NeniaUiState.h>

namespace nenia::app {

void applyPlaybackOptionControl(NeniaPlaylistController &playlist,
                                NeniaTransportControl control);

} // namespace nenia::app
