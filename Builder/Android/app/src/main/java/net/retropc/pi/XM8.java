package net.retropc.pi;

import java.io.File;
import java.text.Normalizer;
import java.util.ArrayList;
import java.util.List;
import java.io.FileInputStream;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.FileOutputStream;
import java.io.BufferedWriter;
import java.io.OutputStreamWriter;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.SocketTimeoutException;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import android.content.Context;
import android.content.SharedPreferences;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import android.util.Base64;

import org.libsdl.app.SDLActivity;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.WindowManager;
import androidx.core.content.ContextCompat;
import android.content.pm.PackageManager;
import android.content.pm.ActivityInfo;
import androidx.core.app.ActivityCompat;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.DisplayCutout;
import android.view.RoundedCorner;
import android.graphics.Insets;
import android.widget.Toast;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.text.InputType;
import androidx.core.os.EnvironmentCompat;
import androidx.documentfile.provider.DocumentFile;
import androidx.annotation.RequiresApi;
import android.os.ParcelFileDescriptor;

public class XM8 extends SDLActivity {
    // Called on the screenshot writer thread; only permission UI runs on the UI thread.
    public String saveScreenshot(byte[] png, String stem) {
        if (Build.VERSION.SDK_INT >= 23 && Build.VERSION.SDK_INT <= 28 &&
                ContextCompat.checkSelfPermission(this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        != PackageManager.PERMISSION_GRANTED) {
            runOnUiThread(() -> ActivityCompat.requestPermissions(this,
                    new String[]{android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, 82));
            return "WAIT:Allow storage access, then save the screenshot again";
        }
        if (Build.VERSION.SDK_INT >= 29) return saveScreenshotMediaStore(png, stem);
        File file = null;
        try {
            File folder = new File(Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_PICTURES), "XM8/Screenshots");
            if (!folder.isDirectory() && !folder.mkdirs()) {
                return "Cannot create screenshot folder: " + folder;
            }
            for (int i = 0; i < 10000; i++) {
                File candidate = new File(folder, stem + (i == 0 ? "" : "_" + i) + ".png");
                if (candidate.createNewFile()) { file = candidate; break; }
            }
            if (file == null) return "Cannot allocate a unique screenshot filename";
            try (FileOutputStream output = new FileOutputStream(file)) {
                output.write(png);
                output.getFD().sync();
            }
            android.media.MediaScannerConnection.scanFile(this,
                    new String[]{file.getAbsolutePath()}, new String[]{"image/png"}, null);
            return "OK:" + file.getAbsolutePath();
        } catch (Exception e) {
            if (file != null && !file.delete()) Log.w(LOG_TAG, "Could not remove incomplete screenshot: " + file);
            return "Screenshot save failed: " + e.getMessage();
        }
    }

    @RequiresApi(29)
    private String saveScreenshotMediaStore(byte[] png, String stem) {
        android.content.ContentResolver resolver = getContentResolver();
        Uri uri = null;
        try {
            android.content.ContentValues values = new android.content.ContentValues();
            values.put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, stem + ".png");
            values.put(android.provider.MediaStore.Images.Media.MIME_TYPE, "image/png");
            values.put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/XM8/Screenshots");
            values.put(android.provider.MediaStore.Images.Media.IS_PENDING, 1);
            uri = resolver.insert(android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            if (uri == null) throw new IOException("MediaStore could not create the image");
            try (java.io.OutputStream output = resolver.openOutputStream(uri, "w")) {
                if (output == null) throw new IOException("MediaStore could not open the image");
                output.write(png);
            }
            String actualName = stem + ".png";
            try (android.database.Cursor cursor = resolver.query(uri,
                    new String[]{android.provider.MediaStore.Images.Media.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) actualName = cursor.getString(0);
            }
            values.clear();
            values.put(android.provider.MediaStore.Images.Media.IS_PENDING, 0);
            if (resolver.update(uri, values, null, null) != 1) throw new IOException("MediaStore could not publish the image");
            return "OK:Pictures/XM8/Screenshots/" + actualName;
        } catch (Exception e) {
            if (uri != null) {
                try { resolver.delete(uri, null, null); }
                catch (Exception cleanup) { Log.w(LOG_TAG, "Could not remove incomplete screenshot", cleanup); }
            }
            return "Screenshot save failed: " + e.getMessage();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == 82) {
            boolean granted = grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED;
            Toast.makeText(this, granted ? "Storage access allowed. Save the screenshot again." :
                    "Screenshot not saved. Allow storage access in Android app settings, then retry.",
                    Toast.LENGTH_LONG).show();
        }
    }

    // log
    private static final String LOG_TAG = "XM8";
    private static final int ROTATION_AUTO = 0;
    private static final int ROTATION_LANDSCAPE = 1;
    private static final int ROTATION_PORTRAIT = 2;
    private int mRotationMode = ROTATION_AUTO;

    private void installSafeAreaInsets() {
        if (mLayout == null) return;

        mLayout.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
                int top;
                int bottom;

                // The portrait game/control layout needs room above and below
                // its full-width panels. Do not inset landscape or the left /
                // right edges: those margins are both visually excessive and
                // unnecessary for the existing aspect-correct landscape view.
                if (getResources().getConfiguration().orientation
                        != Configuration.ORIENTATION_PORTRAIT) {
                    view.setPadding(0, 0, 0, 0);
                    return insets;
                }

                if (Build.VERSION.SDK_INT >= 30) {
                    Insets safe = insets.getInsetsIgnoringVisibility(
                            WindowInsets.Type.displayCutout());
                    top = safe.top;
                    bottom = safe.bottom;
                } else {
                    top = 0;
                    bottom = 0;
                    if (Build.VERSION.SDK_INT >= 28) {
                        DisplayCutout cutout = insets.getDisplayCutout();
                        if (cutout != null) {
                            top = Math.max(top, cutout.getSafeInsetTop());
                            bottom = Math.max(bottom, cutout.getSafeInsetBottom());
                        }
                    }
                }

                if (Build.VERSION.SDK_INT >= 31) {
                    RoundedCorner topLeft = insets.getRoundedCorner(
                            RoundedCorner.POSITION_TOP_LEFT);
                    RoundedCorner topRight = insets.getRoundedCorner(
                            RoundedCorner.POSITION_TOP_RIGHT);
                    RoundedCorner bottomLeft = insets.getRoundedCorner(
                            RoundedCorner.POSITION_BOTTOM_LEFT);
                    RoundedCorner bottomRight = insets.getRoundedCorner(
                            RoundedCorner.POSITION_BOTTOM_RIGHT);
                    top = Math.max(top, Math.max(radiusOf(topLeft), radiusOf(topRight)));
                    bottom = Math.max(bottom, Math.max(radiusOf(bottomLeft), radiusOf(bottomRight)));
                }

                view.setPadding(0, top, 0, bottom);
                return insets;
            }
        });
        mLayout.requestApplyInsets();
    }

    private static int radiusOf(RoundedCorner corner) {
        return corner == null ? 0 : corner.getRadius();
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        // XM8 supports both the legacy landscape layout and the portrait
        // game/control split.  SDL's default picks landscape from its initial
        // 640x400 window size, so keep the Activity responsive to user rotation.
        applyRotationMode();
    }

    public void setRotationMode(final int mode) {
        mRotationMode = (mode == ROTATION_LANDSCAPE || mode == ROTATION_PORTRAIT)
                ? mode : ROTATION_AUTO;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (!isFinishing()) applyRotationMode();
            }
        });
    }

    private void applyRotationMode() {
        switch (mRotationMode) {
        case ROTATION_LANDSCAPE:
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
            break;
        case ROTATION_PORTRAIT:
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
            break;
        default:
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_FULL_USER);
            break;
        }
    }

    // directory and filename
    private static final String ROM_DIRECTORY = "/XM8/";
    private static final String PC88_FILENAME = "PC88.ROM";
    private static final String N80_FILENAME = "N80.ROM";
    private static final String N88_FILENAME = "N88.ROM";
    private static final String DISK_FILENAME = "DISK.ROM";
    private static final String N88EXT0_FILENAME = "N88_0.ROM";
    private static final String N88EXT1_FILENAME = "N88_1.ROM";
    private static final String N88EXT2_FILENAME = "N88_2.ROM";
    private static final String N88EXT3_FILENAME = "N88_3.ROM";
    private static final String KANJI1_FILENAME = "KANJI1.ROM";

    // message text
    private static final String TOAST_ROM_MESSAGE = "The ROM file is not found: ";

    // storage access framework
    private static final String REQUEST_FILENAME = "request.dat";
    private static final String URI_FILENAME = "uri.dat";
    private String mAbsPath;
    private String mExtDir;
    private String mTreeUri;

    // control flag
    private boolean mROMError;

    // RetroAchievements: HTTP runs off the SDL/UI thread.  These members are
    // deliberately owned by the Activity so background work can be cancelled
    // before the JNI activity reference is released.
    private final ExecutorService mRaHttpExecutor = Executors.newCachedThreadPool();
    private final Map<Long, HttpURLConnection> mRaConnections = new ConcurrentHashMap<>();
    private final Map<Long, Future<?>> mRaHttpTasks = new ConcurrentHashMap<>();
    private static final String RA_CREDENTIALS = "ra_credentials";
    private static final String RA_KEY_ALIAS = "net.retropc.pi.XM8.RetroAchievements";
    private static final int RA_HTTP_SUCCESS = 0;
    private static final int RA_HTTP_CLIENT_ERROR = 1;
    private static final int RA_HTTP_RETRYABLE = 2;
    private static final int RA_HTTP_CANCELED = 3;
    private static final int RA_HTTP_TIMEOUT = 4;
    private static final int RA_HTTP_OVERSIZE = 5;

    private boolean isRaRuntimeSupported() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.M;
    }

    private static final class RaResponseTooLargeException extends IOException {
        RaResponseTooLargeException() { super("response too large"); }
    }

    // request id
    private static final int REQUEST_PERMISSION = 1;
    private static final int REQUEST_DOCUMENT = 2;

    // JNI native routine
    private native void nativeIntent(String name);
    private native void nativeBuildVer(int ver);
    private native void nativeAbsDir(String dir);
    private native void nativeExtDir(String dir);
    private native void nativeUri(String treeUri);
    private native void nativeSkipMain(int skip);
    private native void nativeDelete();
    private static native void nativeRaHttpComplete(long requestId, int result,
            int status, String contentType, byte[] body, String error);
    private static native void nativeRaLoginSubmitted(String username, String password);
    private static native void nativeRaLoginCanceled();
    private static native void nativeMenuBackRequested();
    private static native void nativeMouseBackRequested();

    private AlertDialog mRaLoginDialog;
    private EditText mRaLoginUsername;
    private EditText mRaLoginPassword;
    private TextView mRaLoginStatus;
    private Button mRaLoginButton;

    // setup

    @Override
    public void onBackPressed() {
        nativeMenuBackRequested();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK &&
                (event.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) {
            if (event.getAction() == KeyEvent.ACTION_UP) nativeMouseBackRequested();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.i(LOG_TAG, "onCreate");

        // initialize flags
        mROMError = false;

        // super class
        super.onCreate(savedInstanceState);

        // immersive full-screen mode or dim status bar / navigation icon
        setupWindow();
        installSafeAreaInsets();

        // set Build.VERSION.SDK_INT and app files directory
        nativeBuildVer(Build.VERSION.SDK_INT);
        mAbsPath = getAppFilesDirectory().getAbsolutePath();
        nativeAbsDir(mAbsPath);

        // process intent
        if (Intent.ACTION_VIEW.equals(getIntent().getAction())) {
            String fname[] = String.valueOf(getIntent().getData()).split("//");
            if (fname.length == 2) {
                nativeIntent(fname[1]);
            }
        }
        else {
            nativeIntent("");
        }
    }

    @Override
    protected void onStart() {
        Log.i(LOG_TAG, "onStart");
        // super class
        super.onStart();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            if (checkStartActivity()) {
                return;
            }

            // read treeUri
            mTreeUri = "";
            try {
                FileInputStream inputStream = openFileInput(URI_FILENAME);
                BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream));
                mTreeUri = reader.readLine();
                reader.close();
            } catch (java.io.FileNotFoundException e) {
                Log.i(LOG_TAG, "treeUri file is not found");
            } catch (java.io.IOException e) {
                Log.i(LOG_TAG, "treeUri file IO Exception");
            }
            nativeUri(mTreeUri);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                // Display a dialog asking the user for permission to access external storage.
                Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                startActivity(intent);
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // API level 23 or later requires self permission to access storage
            if (ContextCompat.checkSelfPermission(this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                if (!ActivityCompat.shouldShowRequestPermissionRationale(this, android.Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
                    // Optional legacy access for image files outside the app directory.
                    ActivityCompat.requestPermissions(this,  new String[]{android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_PERMISSION);
                }
            }
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        Log.i(LOG_TAG, "onWindowFocusChanged():" + hasFocus);

        if (hasFocus) {
            // ROM files are in app-specific storage and do not require
            // WRITE_EXTERNAL_STORAGE on Android 6.0 or later.
            String basepath = mAbsPath + File.separator;

            // check mandatory ROMs
            mROMError = false;
            if (!checkROM(basepath, PC88_FILENAME, false)) {
                // retry with M88 ROM sets
                mROMError = false;

                checkROM(basepath, N80_FILENAME, true);
                checkROM(basepath, N88_FILENAME, true);
                checkROM(basepath, DISK_FILENAME, true);
                checkROM(basepath, N88EXT0_FILENAME, true);
                checkROM(basepath, N88EXT1_FILENAME, true);
                checkROM(basepath, N88EXT2_FILENAME, true);
                checkROM(basepath, N88EXT3_FILENAME, true);
            }
            checkROM(basepath, KANJI1_FILENAME, true);

            // set result to native
            if (mROMError) {
                nativeSkipMain(1);
            } else {
                nativeSkipMain(0);
            }

            // get external storage path
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                String[] extDirs = getExternalStorageDirectories();
                if (extDirs.length > 0 && extDirs[0] != null) {
                    // The first path is the primary app-specific external
                    // storage. Emulators can report additional candidates;
                    // leaving mExtDir unset in that case crashes the SAF
                    // bridge before it can reject an unavailable tree URI.
                    mExtDir = extDirs[0];
                    nativeExtDir(mExtDir);
                }
            }
        }

        // super class
        super.onWindowFocusChanged(hasFocus);
    }

    @Override
    protected void onResume() {
        Log.i(LOG_TAG, "onResume");
        // super class
        super.onResume();

        setupWindow();
    }

    @Override
    protected void onDestroy() {
        Log.i(LOG_TAG, "onDestroy");

        raCancelAllHttp();
        mRaHttpExecutor.shutdownNow();

        // call DeleteGlobalRef()
        nativeDelete();

        // super class
        super.onDestroy();
    }

    // Called from native only when the runtime supports RetroAchievements.
    public void raSendHttp(final long requestId, final String url, final byte[] postData,
            final String contentType, final String userAgent, final int connectTimeoutMs,
            final int totalTimeoutMs, final int maxResponseBytes) {
        if (!isRaRuntimeSupported() || url == null || !url.startsWith("https://") ||
                userAgent == null || userAgent.isEmpty() || maxResponseBytes < 0) {
            nativeRaHttpComplete(requestId, RA_HTTP_CLIENT_ERROR, 0, "", null,
                    "HTTPS URL required");
            return;
        }
        Future<?> task = mRaHttpExecutor.submit(new Runnable() {
            @Override public void run() {
                HttpURLConnection connection = null;
                int result = RA_HTTP_CLIENT_ERROR;
                int status = 0;
                String responseType = "";
                byte[] responseBody = null;
                String error = "HTTP request failed";
                try {
                    connection = (HttpURLConnection) new URL(url).openConnection();
                    mRaConnections.put(requestId, connection);
                    connection.setConnectTimeout(Math.max(1, connectTimeoutMs));
                    connection.setReadTimeout(Math.max(1, totalTimeoutMs));
                    connection.setInstanceFollowRedirects(false);
                    RaHttpHeaders.apply(connection, userAgent);
                    connection.setRequestMethod(postData == null ? "GET" : "POST");
                    if (postData != null) {
                        connection.setDoOutput(true);
                        if (contentType != null && !contentType.isEmpty()) {
                            connection.setRequestProperty("Content-Type", contentType);
                        }
                        connection.getOutputStream().write(postData);
                    }
                    status = connection.getResponseCode();
                    responseType = connection.getContentType();
                    // getContentLengthLong() was added in API 24. The response cap is
                    // already an int, so the API 19-compatible variant is sufficient.
                    long length = connection.getContentLength();
                    if (length > maxResponseBytes) {
                        result = RA_HTTP_OVERSIZE;
                        error = "HTTP response exceeds limit";
                    } else {
                        InputStream input = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
                        responseBody = readRaResponse(input, maxResponseBytes);
                        result = RA_HTTP_SUCCESS;
                        error = "";
                    }
                } catch (RaResponseTooLargeException e) {
                    result = RA_HTTP_OVERSIZE;
                    error = "HTTP response exceeds limit";
                } catch (SocketTimeoutException e) {
                    result = RA_HTTP_TIMEOUT;
                    error = "HTTP request timed out";
                } catch (IOException e) {
                    result = Thread.currentThread().isInterrupted() ? RA_HTTP_CANCELED : RA_HTTP_RETRYABLE;
                    error = "HTTP transport error";
                } catch (SecurityException e) {
                    result = RA_HTTP_CLIENT_ERROR;
                    error = "HTTP security error";
                } finally {
                    mRaConnections.remove(requestId);
                    mRaHttpTasks.remove(requestId);
                    if (connection != null) connection.disconnect();
                }
                nativeRaHttpComplete(requestId, result, status,
                        responseType == null ? "" : responseType, responseBody, error);
            }
        });
        mRaHttpTasks.put(requestId, task);
    }

    private static byte[] readRaResponse(InputStream input, int maxBytes) throws IOException {
        if (input == null) return new byte[0];
        try {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] chunk = new byte[8192];
            for (int count; (count = input.read(chunk)) != -1;) {
                if (output.size() > maxBytes - count) throw new RaResponseTooLargeException();
                output.write(chunk, 0, count);
            }
            return output.toByteArray();
        } finally {
            input.close();
        }
    }

    public void raCancelHttp(long requestId) {
        HttpURLConnection connection = mRaConnections.remove(requestId);
        if (connection != null) connection.disconnect();
        Future<?> task = mRaHttpTasks.remove(requestId);
        if (task != null) task.cancel(true);
    }

    public void raCancelAllHttp() {
        for (Long requestId : mRaConnections.keySet()) raCancelHttp(requestId);
        for (Long requestId : mRaHttpTasks.keySet()) raCancelHttp(requestId);
    }

    // Android owns the password UI. The scroll container remains usable when
    // the landscape IME reduces the available height.
    public void raShowLogin(final String initialUsername) {
        if (!isRaRuntimeSupported()) return;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (isFinishing() || isDestroyed()) return;
                if (mRaLoginDialog != null && mRaLoginDialog.isShowing()) return;

                final int padding = (int)(20 * getResources().getDisplayMetrics().density);
                final ScrollView scroll = new ScrollView(XM8.this);
                scroll.setFillViewport(true);
                final LinearLayout content = new LinearLayout(XM8.this);
                content.setOrientation(LinearLayout.VERTICAL);
                content.setPadding(padding, padding, padding, padding);
                scroll.addView(content, new ScrollView.LayoutParams(
                        ScrollView.LayoutParams.MATCH_PARENT,
                        ScrollView.LayoutParams.WRAP_CONTENT));

                mRaLoginUsername = new EditText(XM8.this);
                mRaLoginUsername.setHint("Username");
                mRaLoginUsername.setSingleLine(true);
                mRaLoginUsername.setInputType(InputType.TYPE_CLASS_TEXT |
                        InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
                mRaLoginUsername.setText(initialUsername == null ? "" : initialUsername);
                content.addView(mRaLoginUsername, new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT));

                mRaLoginPassword = new EditText(XM8.this);
                mRaLoginPassword.setHint("Password");
                mRaLoginPassword.setSingleLine(true);
                mRaLoginPassword.setInputType(InputType.TYPE_CLASS_TEXT |
                        InputType.TYPE_TEXT_VARIATION_PASSWORD);
                mRaLoginPassword.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE);
                content.addView(mRaLoginPassword, new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT));

                mRaLoginStatus = new TextView(XM8.this);
                content.addView(mRaLoginStatus, new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT));

                final int buttonGap = (int)(8 * getResources().getDisplayMetrics().density);
                final LinearLayout actions = new LinearLayout(XM8.this);
                actions.setOrientation(LinearLayout.HORIZONTAL);
                mRaLoginButton = new Button(XM8.this);
                mRaLoginButton.setText("Login");
                final Button cancelButton = new Button(XM8.this);
                cancelButton.setText(android.R.string.cancel);
                final LinearLayout.LayoutParams loginButtonParams =
                        new LinearLayout.LayoutParams(0,
                                LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
                loginButtonParams.setMargins(0, padding, buttonGap / 2, 0);
                final LinearLayout.LayoutParams cancelButtonParams =
                        new LinearLayout.LayoutParams(0,
                                LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
                cancelButtonParams.setMargins(buttonGap / 2, padding, 0, 0);
                actions.addView(mRaLoginButton, loginButtonParams);
                actions.addView(cancelButton, cancelButtonParams);
                content.addView(actions, new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT));

                final View.OnFocusChangeListener revealFocusedField =
                        new View.OnFocusChangeListener() {
                    @Override public void onFocusChange(final View view, boolean focused) {
                        if (focused) scroll.post(new Runnable() {
                            @Override public void run() {
                                scroll.smoothScrollTo(0, Math.max(0, view.getTop() - padding));
                            }
                        });
                    }
                };
                mRaLoginUsername.setOnFocusChangeListener(revealFocusedField);
                mRaLoginPassword.setOnFocusChangeListener(revealFocusedField);
                mRaLoginPassword.setOnEditorActionListener((view, actionId, event) -> {
                    if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE) {
                        submitRaLogin();
                        return true;
                    }
                    return false;
                });

                mRaLoginDialog = new AlertDialog.Builder(XM8.this)
                        .setTitle("RetroAchievements Login")
                        .setView(scroll)
                        .create();
                final AlertDialog loginDialog = mRaLoginDialog;
                mRaLoginDialog.setCanceledOnTouchOutside(false);
                mRaLoginDialog.setOnCancelListener(dialog -> nativeRaLoginCanceled());
                mRaLoginDialog.setOnDismissListener(dialog -> {
                    if (mRaLoginDialog != loginDialog) return;
                    mRaLoginDialog = null;
                    mRaLoginUsername = null;
                    mRaLoginPassword = null;
                    mRaLoginStatus = null;
                    mRaLoginButton = null;
                });
                mRaLoginButton.setOnClickListener(view -> submitRaLogin());
                cancelButton.setOnClickListener(view -> loginDialog.cancel());
                mRaLoginDialog.setOnShowListener(dialog -> {
                    EditText first = mRaLoginUsername.getText().length() == 0 ?
                            mRaLoginUsername : mRaLoginPassword;
                    first.requestFocus();
                    final android.view.Window window = loginDialog.getWindow();
                    if (window == null) return;
                    window.setSoftInputMode(
                            WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE |
                            WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);

                    // Keep the platform dialog styling, but give the login fields
                    // about 1.5 times the normal dialog width. Do not let it occupy
                    // almost all of a narrow landscape display.
                    final int currentWidth = window.getDecorView().getWidth();
                    final int maximumWidth = (int)(getResources().getDisplayMetrics().widthPixels * 0.85f);
                    final int widerWidth = Math.min((int)(currentWidth * 1.5f), maximumWidth);
                    if (widerWidth > currentWidth) {
                        window.setLayout(widerWidth, WindowManager.LayoutParams.WRAP_CONTENT);
                    }
                });
                mRaLoginDialog.show();
            }
        });
    }

    private void submitRaLogin() {
        if (mRaLoginDialog == null || mRaLoginUsername == null || mRaLoginPassword == null) return;
        String username = mRaLoginUsername.getText().toString();
        String password = mRaLoginPassword.getText().toString();
        if (username.length() == 0 || password.length() == 0) {
            mRaLoginStatus.setText("Enter username and password");
            return;
        }
        mRaLoginStatus.setText("Logging in…");
        mRaLoginUsername.setEnabled(false);
        mRaLoginPassword.setEnabled(false);
        mRaLoginButton.setEnabled(false);
        nativeRaLoginSubmitted(username, password);
    }

    public void raSetLoginResult(final String message, final boolean success) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (mRaLoginDialog == null) return;
                if (success) {
                    mRaLoginDialog.dismiss();
                    return;
                }
                mRaLoginStatus.setText(message == null ? "Login failed" : message);
                mRaLoginUsername.setEnabled(true);
                mRaLoginPassword.setEnabled(true);
                mRaLoginButton.setEnabled(true);
                mRaLoginPassword.requestFocus();
            }
        });
    }

    public boolean raHasNetwork() {
        if (!isRaRuntimeSupported()) return false;
        ConnectivityManager manager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (manager == null) return false;
        NetworkInfo network = manager.getActiveNetworkInfo();
        return network != null && network.isConnected();
    }

    @RequiresApi(Build.VERSION_CODES.M)
    private SecretKey raCredentialKey() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (!store.containsAlias(RA_KEY_ALIAS)) {
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,
                    "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(RA_KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .build());
            generator.generateKey();
        }
        return ((KeyStore.SecretKeyEntry) store.getEntry(RA_KEY_ALIAS, null)).getSecretKey();
    }

    private String raCredentialName(String username) {
        return Base64.encodeToString(username.getBytes(java.nio.charset.StandardCharsets.UTF_8),
                Base64.NO_WRAP | Base64.URL_SAFE);
    }

    public boolean raSaveCredential(String username, byte[] token) {
        if (!isRaRuntimeSupported() || username == null || token == null || token.length == 0) return false;
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, raCredentialKey());
            byte[] encrypted = cipher.doFinal(token);
            byte[] iv = cipher.getIV();
            byte[] blob = new byte[iv.length + encrypted.length];
            System.arraycopy(iv, 0, blob, 0, iv.length);
            System.arraycopy(encrypted, 0, blob, iv.length, encrypted.length);
            return getSharedPreferences(RA_CREDENTIALS, MODE_PRIVATE).edit()
                    .putString(raCredentialName(username), Base64.encodeToString(blob, Base64.NO_WRAP))
                    .commit();
        } catch (Exception e) { return false; }
    }

    public byte[] raLoadCredential(String username) {
        if (!isRaRuntimeSupported() || username == null) return null;
        try {
            String text = getSharedPreferences(RA_CREDENTIALS, MODE_PRIVATE)
                    .getString(raCredentialName(username), null);
            if (text == null) return null;
            byte[] blob = Base64.decode(text, Base64.NO_WRAP);
            if (blob.length <= 12) return null;
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, raCredentialKey(), new GCMParameterSpec(128, blob, 0, 12));
            return cipher.doFinal(blob, 12, blob.length - 12);
        } catch (Exception e) { return null; }
    }

    public boolean raDeleteCredential(String username) {
        if (!isRaRuntimeSupported()) return false;
        if (username == null) return true;
        return getSharedPreferences(RA_CREDENTIALS, MODE_PRIVATE).edit()
                .remove(raCredentialName(username)).commit();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        Log.i(LOG_TAG, "onNewIntent()");
        if (Intent.ACTION_VIEW.equals(intent.getAction())) {
            String fname[] = String.valueOf(intent.getData()).split("//");
            if (fname.length == 2) {
                nativeIntent(fname[1]);
            }
        }

        super.onNewIntent(intent);
    }


    // setup window
    private void  setupWindow() {
        setWindowStyle(true);
    }

    // check ROM
    private boolean checkROM(String basepath, String filename, boolean alert) {
        if (mROMError) {
            return false;
        }

        // get result as whether the file exists or not
        File file = new File(basepath + filename);
        boolean result = file.exists();

        // save result
        if (!result) {
            mROMError = true;
        }

        // check result
        if (!result && alert) {
            // toast
            Toast.makeText(this, TOAST_ROM_MESSAGE + basepath + filename, Toast.LENGTH_LONG).show();
        }

        return result;
    }

    // request activity on next launch (called from native)
    public void requestActivity() {
        try {
            FileOutputStream outputStream = this.openFileOutput(REQUEST_FILENAME, MODE_PRIVATE);
            BufferedWriter writer = new BufferedWriter(new OutputStreamWriter(outputStream));
            writer.write(REQUEST_FILENAME);
            writer.flush();
            writer.close();
        } catch (java.io.FileNotFoundException e) {
            Log.i(LOG_TAG, "requestActivity:java.io.FileNotFoundException");
        } catch (java.io.IOException e) {
            Log.i(LOG_TAG, "requestActivity:java.io.IOException");
        }
    }

    // check start activity to grant to access storage
    @RequiresApi(Build.VERSION_CODES.LOLLIPOP)
    private boolean checkStartActivity() {
        try {
            FileInputStream inputStream = this.openFileInput(REQUEST_FILENAME);
            Log.i(LOG_TAG, "request file is found, try delete");
            inputStream.close();
            if (deleteFile(REQUEST_FILENAME)) {
                // start activity
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                startActivityForResult(intent, REQUEST_DOCUMENT);
                return true;
            }
        } catch (java.io.FileNotFoundException e) {
            Log.i(LOG_TAG, "request file is not found");
            return false;
        } catch (java.io.IOException e) {
            Log.i(LOG_TAG, "checkStartActivity:java.io.IOException");
            return false;
        }
        return false;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.i(LOG_TAG, "onActivityResult() resultCode:" + resultCode);
        // super class
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == REQUEST_DOCUMENT && resultCode == Activity.RESULT_OK) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
                // get persistable permission
                getContentResolver().takePersistableUriPermission(data.getData(),
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            }

            // save Uri to local file
            Log.i(LOG_TAG, "saveUri:" + data.getDataString());
            try {
                FileOutputStream outputStream = this.openFileOutput(URI_FILENAME, MODE_PRIVATE);
                BufferedWriter writer = new BufferedWriter(new OutputStreamWriter(outputStream));
                writer.write(data.getDataString());
                writer.flush();
                writer.close();
            } catch (java.io.FileNotFoundException e) {
                Log.i(LOG_TAG, "saveUri:java.io.FileNotFoundException");
            } catch (java.io.IOException e) {
                Log.i(LOG_TAG, "saveUri:java.io.IOException");
            }
        }
    }

    // clear treeUri
    public void clearTreeUri() {
        try {
            FileInputStream inputStream = this.openFileInput(URI_FILENAME);
            Log.i(LOG_TAG, "uri file is found, try delete");
            inputStream.close();
            if (deleteFile(URI_FILENAME)) {
                mTreeUri = "";
            }
        } catch (java.io.FileNotFoundException e) {
            Log.i(LOG_TAG, "clearTreeUri:java.io.FileNotFoundException");
        } catch  (java.io.IOException e) {
            Log.i(LOG_TAG, "clearTreeUri:java.io.IOException");
        }
    }

    // get file descriptor from treeUri
    public int getFileDescriptor(String file, int type) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            if (file == null || mExtDir == null || mExtDir.length() == 0 ||
                    mTreeUri == null || mTreeUri.length() == 0) {
                return -1;
            }
            String[] extDirSplit = mExtDir.split("/");
            String[] targetSplit = file.split("/");

            // check count of '/'
            int length = targetSplit.length;
            if (length <= extDirSplit.length) {
                return -1;
            }

            // compare with mExtDir
            int count = 0;
            for (String one : extDirSplit) {
                if (!one.equals(targetSplit[count])) {
                    return -1;
                }
                count++;
            }

            // create DocumentFile from mTreeUri
            Uri treeUri = Uri.parse(mTreeUri);
            DocumentFile treeFile =  DocumentFile.fromTreeUri(this, treeUri);
            if (treeFile == null) {
                return -1;
            }

            // loop DocumentFile.findFile() on each directory
            DocumentFile docFile = null;
            while (count < length) {
                if (docFile == null) {
                    docFile = treeFile.findFile(targetSplit[count]);
                }
                else {
                    docFile = docFile.findFile(targetSplit[count]);
                }
                if (docFile == null) {
                    return -1;
                }
                count++;
            }
            if (docFile == null) {
                return -1;
            }

            // try to open
            String mode = "rw";
            if (type != 0) {
                // read and write access and truncate if file exists
                mode = "rwt";
            }
            try {
                ParcelFileDescriptor pfd = this.getContentResolver().openFileDescriptor(docFile.getUri(), mode);
                int fd = pfd.detachFd();
                Log.i(LOG_TAG,"getFileDescriptor path:" + file + " fd:" + fd);
                return fd;
            } catch (java.io.FileNotFoundException e) {
                Log.i(LOG_TAG, "getFileDescriptor:FileNotFoundException");
            }
        }
        return -1;
    }

    private File getAppFilesDirectory() {
        File externalFilesDir = getExternalFilesDir(null);
        if (externalFilesDir != null) {
            return externalFilesDir;
        }

        Log.w(LOG_TAG, "External files directory is unavailable; using internal storage");
        return getFilesDir();
    }

    // get external storage path
    // https://stackoverflow.com/questions/36766016/how-to-get-sd-card-path-in-android6-0-programmatically/40205116/#40205116
    private String[] getExternalStorageDirectories() {
        List<String> results = new ArrayList<>();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) { //Method 1 for KitKat & above
            File[] externalDirs = getExternalFilesDirs(null);

            for (File file : externalDirs) {
                if (file == null) {
                    continue;
                }
                String path = file.getPath().split("/Android")[0];

                boolean addPath = false;

                if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    addPath = Environment.isExternalStorageRemovable(file);
                }
                else{
                    addPath = Environment.MEDIA_MOUNTED.equals(EnvironmentCompat.getStorageState(file));
                }
                if(addPath){
                    results.add(path);
                }
            }
        }

        String[] storageDirectories = new String[results.size()];
        for(int i=0; i<results.size(); ++i) {
            storageDirectories[i] = results.get(i);
        }

        return storageDirectories;
    }

    // Convert to NFC (called from native)
     public static String convertToNFC(String input) {
         return Normalizer.normalize(input, Normalizer.Form.NFC);
     }
}
