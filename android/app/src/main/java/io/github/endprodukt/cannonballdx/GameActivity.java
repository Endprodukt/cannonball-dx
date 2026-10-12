package io.github.endprodukt.cannonballdx;

import android.content.Context;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.Display;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * The game itself. SDL owns the surface, input and lifecycle; the engine is
 * libmain.so, built from the repository's root CMakeLists.txt.
 */
public class GameActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestHighestRefreshRate();
    }

    /**
     * Displays with adaptive refresh rate otherwise run apps at 60 Hz unless
     * they ask for more. The engine paces itself to its own 30/60/120 fps
     * setting, so the fastest mode is always the right one to request.
     */
    private void requestHighestRefreshRate() {
        if (Build.VERSION.SDK_INT < 23) {
            return;
        }
        Display display = getWindowManager().getDefaultDisplay();
        if (display == null) {
            return;
        }
        float best = 0f;
        for (Display.Mode mode : display.getSupportedModes()) {
            best = Math.max(best, mode.getRefreshRate());
        }
        if (best > 0f) {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.preferredRefreshRate = best;
            getWindow().setAttributes(params);
        }
    }

    /**
     * Called from native code (touchcontrols.cpp) for touch ticks and rumble.
     * The vibration is tagged as game/media output so that it follows the
     * phone's media vibration setting instead of its touch-feedback setting.
     *
     * @param durationMs length in milliseconds; 0 or less stops the vibrator
     * @param strength   0..1
     */
    public void vibratePhone(int durationMs, float strength) {
        Vibrator vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) {
            return;
        }
        if (durationMs <= 0) {
            vibrator.cancel();
            return;
        }

        if (Build.VERSION.SDK_INT < 26) {
            vibrator.vibrate(durationMs);
            return;
        }

        int amplitude = VibrationEffect.DEFAULT_AMPLITUDE;
        if (vibrator.hasAmplitudeControl()) {
            amplitude = Math.max(1, Math.min(255, Math.round(strength * 255f)));
        }
        VibrationEffect effect = VibrationEffect.createOneShot(durationMs, amplitude);

        if (Build.VERSION.SDK_INT >= 33) {
            vibrator.vibrate(effect,
                    VibrationAttributes.createForUsage(VibrationAttributes.USAGE_MEDIA));
        } else {
            vibrator.vibrate(effect,
                    new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_GAME)
                            .build());
        }
    }
}
