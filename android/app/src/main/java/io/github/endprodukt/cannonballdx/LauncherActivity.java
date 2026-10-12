package io.github.endprodukt.cannonballdx;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * Prepares the game's files directory before SDL starts.
 *
 * The engine uses ordinary relative file paths (roms/, res/, config.xml), so
 * everything lives in the app's own files directory:
 *
 *   - the resources bundled in the APK are unpacked there after every update;
 *   - the OutRun ROM set, which cannot be distributed with the app, is copied
 *     there from a file the user picks with the system file picker.
 *
 * With a ROM set in place the launcher is invisible and starts the game
 * immediately.
 */
public class LauncherActivity extends Activity {

    /** App shortcut action: always show the import screen. */
    public static final String ACTION_IMPORT = "io.github.endprodukt.cannonballdx.IMPORT";

    private static final String TAG = "CannonBallDX";
    private static final int REQUEST_IMPORT = 1;

    private static final String PREFS = "launcher";
    private static final String PREF_ASSETS_STAMP = "assets_stamp";

    /** Written by the engine when it rejects the ROM set. */
    private static final String ROM_ERROR_FLAG = "rom_error.flag";

    /** Custom music, e.g. 04_AHA_Take_On_Me.mp3; everything else is ROM data. */
    private static final Pattern CUSTOM_MUSIC =
            Pattern.compile("^\\d{2}[-_].+\\.(wav|mp3|ym|bin)$", Pattern.CASE_INSENSITIVE);

    private File dataDir;
    private TextView statusView;
    private Button playButton;
    private Button importButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        dataDir = resolveDataDir();
        new File(dataDir, "roms").mkdirs();
        new File(dataDir, "res").mkdirs();

        try {
            unpackAssetsIfNeeded();
        } catch (IOException e) {
            Log.e(TAG, "Unable to unpack the bundled resources", e);
            buildUi();
            setStatus(getString(R.string.status_assets_failed, e.getMessage()));
            playButton.setEnabled(false);
            importButton.setEnabled(false);
            return;
        }

        File romError = new File(dataDir, ROM_ERROR_FLAG);
        boolean romRejected = romError.exists();
        boolean importRequested = ACTION_IMPORT.equals(getIntent().getAction());

        if (hasRoms() && !romRejected && !importRequested) {
            startGame();
            return;
        }

        buildUi();
        if (romRejected) {
            romError.delete();
            setStatus(getString(R.string.status_rom_rejected));
        } else if (hasRoms()) {
            setStatus(getString(R.string.status_ready));
        } else {
            setStatus(getString(R.string.status_no_roms));
        }
        playButton.setEnabled(hasRoms());
    }

    /** Must match android_prepare_storage() in src/main/main.cpp. */
    private File resolveDataDir() {
        File dir = getExternalFilesDir(null);
        return dir != null ? dir : getFilesDir();
    }

    private boolean hasRoms() {
        File[] files = new File(dataDir, "roms").listFiles();
        if (files == null) {
            return false;
        }
        for (File file : files) {
            if (file.isFile() && !file.getName().equals("roms.txt")) {
                return true;
            }
        }
        return false;
    }

    private void startGame() {
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    // ------------------------------------------------------------------------
    // Bundled resources
    // ------------------------------------------------------------------------

    private void unpackAssetsIfNeeded() throws IOException {
        String stamp;
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            stamp = info.versionCode + ":" + info.lastUpdateTime;
        } catch (Exception e) {
            stamp = "unknown";
        }

        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        boolean complete = new File(dataDir, "res/tilemap.bin").exists();
        if (complete && stamp.equals(prefs.getString(PREF_ASSETS_STAMP, ""))) {
            return;
        }

        // Shipped files are replaced on every update. Files the user added,
        // such as custom music in res/, are left alone.
        unpackAssetDir("res");
        unpackAssetDir("roms");
        prefs.edit().putString(PREF_ASSETS_STAMP, stamp).apply();
    }

    private void unpackAssetDir(String name) throws IOException {
        AssetManager assets = getAssets();
        String[] entries = assets.list(name);
        if (entries == null) {
            return;
        }

        File targetDir = new File(dataDir, name);
        targetDir.mkdirs();

        for (String entry : entries) {
            try (InputStream in = assets.open(name + "/" + entry)) {
                copy(in, new File(targetDir, entry));
            }
        }
    }

    private static void copy(InputStream in, File target) throws IOException {
        // Write to a temporary file first so an interrupted copy can never
        // leave a truncated ROM or resource behind.
        File temp = new File(target.getParentFile(), target.getName() + ".part");
        try (OutputStream out = new FileOutputStream(temp)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        }
        if (target.exists() && !target.delete()) {
            throw new IOException("Cannot replace " + target.getName());
        }
        if (!temp.renameTo(target)) {
            throw new IOException("Cannot write " + target.getName());
        }
    }

    // ------------------------------------------------------------------------
    // Import screen
    // ------------------------------------------------------------------------

    private void buildUi() {
        int pad = (int) (24 * getResources().getDisplayMetrics().density);

        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        column.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(R.string.app_name);
        title.setTextSize(26);
        title.setGravity(Gravity.CENTER);
        column.addView(title);

        TextView intro = new TextView(this);
        intro.setText(getString(R.string.import_intro, new File(dataDir, "roms").getPath()));
        intro.setTextSize(15);
        intro.setPadding(0, pad / 2, 0, pad / 2);
        column.addView(intro);

        statusView = new TextView(this);
        statusView.setTextSize(16);
        statusView.setGravity(Gravity.CENTER);
        statusView.setPadding(0, 0, 0, pad / 2);
        column.addView(statusView);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);

        importButton = new Button(this);
        importButton.setText(R.string.button_import);
        importButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                pickFiles();
            }
        });
        buttons.addView(importButton);

        playButton = new Button(this);
        playButton.setText(R.string.button_play);
        playButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startGame();
            }
        });
        buttons.addView(playButton);

        column.addView(buttons);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(column);
        setContentView(scroll);
    }

    private void setStatus(String text) {
        if (statusView != null) {
            statusView.setText(text);
        }
    }

    private void pickFiles() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // ROM files have arbitrary extensions (epr-10187.88), so no MIME filter.
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try {
            startActivityForResult(intent, REQUEST_IMPORT);
        } catch (Exception e) {
            setStatus(getString(R.string.status_no_picker));
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode != REQUEST_IMPORT || resultCode != RESULT_OK || data == null) {
            return;
        }

        final List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); i++) {
                uris.add(clip.getItemAt(i).getUri());
            }
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        if (uris.isEmpty()) {
            return;
        }

        importButton.setEnabled(false);
        playButton.setEnabled(false);
        setStatus(getString(R.string.status_importing));

        new Thread(new Runnable() {
            @Override
            public void run() {
                int imported = 0;
                String error = null;

                for (Uri uri : uris) {
                    try {
                        importFile(uri);
                        imported++;
                    } catch (Exception e) {
                        Log.e(TAG, "Import failed for " + uri, e);
                        error = e.getMessage() != null ? e.getMessage() : e.toString();
                    }
                }

                final int count = imported;
                final String failure = error;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        importFinished(count, failure);
                    }
                });
            }
        }, "import").start();
    }

    private void importFile(Uri uri) throws IOException {
        String name = sanitizeName(displayName(uri));
        File targetDir = new File(
                dataDir, CUSTOM_MUSIC.matcher(name).matches() ? "res" : "roms");
        targetDir.mkdirs();

        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) {
                throw new IOException("Cannot open " + name);
            }
            copy(in, new File(targetDir, name));
        }
    }

    private String displayName(Uri uri) {
        String name = null;
        try (Cursor cursor = getContentResolver().query(
                uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                name = cursor.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "No display name for " + uri, e);
        }
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment();
        }
        return name != null ? name : "outrun.zip";
    }

    /** Keep only the file name; the picker must never choose a directory. */
    private static String sanitizeName(String name) {
        name = name.replace('\\', '/');
        name = name.substring(name.lastIndexOf('/') + 1).trim();
        if (name.isEmpty() || name.equals(".") || name.equals("..")) {
            name = "outrun.zip";
        }
        // The engine's legacy ROM file names are lower case.
        return name.toLowerCase(Locale.ROOT);
    }

    private void importFinished(int imported, String failure) {
        importButton.setEnabled(true);
        playButton.setEnabled(hasRoms());

        if (failure != null) {
            setStatus(getString(R.string.status_import_failed, failure));
        } else if (hasRoms()) {
            setStatus(getString(R.string.status_imported, imported));
        } else {
            setStatus(getString(R.string.status_no_roms));
        }
    }
}
