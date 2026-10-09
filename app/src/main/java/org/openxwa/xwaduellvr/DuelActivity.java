package org.openxwa.xwaduellvr;

import org.libsdl.app.SDLActivity;

/**
 * XWADuelVR: boots straight into the cockpit duel. No menus, no dialogs.
 */
public final class DuelActivity extends SDLActivity {
    @Override protected String[] getLibraries() {
        return new String[]{"openxr_loader", "SDL3", "XWADuelVR"};
    }
}
