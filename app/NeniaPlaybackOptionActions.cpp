#include "NeniaPlaybackOptionActions.h"

namespace nenia::app {

void applyPlaybackOptionControl(NeniaPlaylistController &playlist,
                                NeniaTransportControl control)
{
    switch (control) {
    case NeniaTransportControl::Loop: {
        NeniaLoopMode nextLoopMode = NeniaLoopMode::Off;
        switch (playlist.loopMode()) {
        case NeniaLoopMode::Off:
            nextLoopMode = NeniaLoopMode::RepeatOne;
            break;
        case NeniaLoopMode::RepeatOne:
            nextLoopMode = NeniaLoopMode::RepeatAll;
            break;
        case NeniaLoopMode::RepeatAll:
            nextLoopMode = NeniaLoopMode::Off;
            break;
        }
        playlist.setLoopMode(nextLoopMode);
        break;
    }
    case NeniaTransportControl::Shuffle:
        playlist.setShuffleEnabled(!playlist.shuffleEnabled());
        break;
    case NeniaTransportControl::Previous:
    case NeniaTransportControl::PlayPause:
    case NeniaTransportControl::Next:
    case NeniaTransportControl::None:
        break;
    }
}

} // namespace nenia::app
