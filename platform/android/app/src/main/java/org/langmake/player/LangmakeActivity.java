package org.langmake.player;

import android.util.Base64;
import android.util.Log;
import org.json.JSONArray;
import org.json.JSONObject;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.FileVisitResult;
import java.nio.file.attribute.BasicFileAttributes;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Set;

/** Small offline SDL host. Conversation, the Android build toolchain and credentials stay on the PC. */
public final class LangmakeActivity extends SDLActivity {
    private static final String TAG = "Langmake";
    private static final long MAX_FILE = 256L * 1024 * 1024;
    private static final long MAX_BUNDLE = 1024L * 1024 * 1024;
    private String[] launchArguments;

    @Override protected String[] getLibraries() { return new String[] { "main" }; }

    @Override public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // SDL otherwise broadens resizable windows to FULL_USER, overriding the manifest's game setting.
        setRequestedOrientation(BuildConfig.LANGMAKE_SCREEN_ORIENTATION);
    }

    @Override protected String[] getArguments() {
        if (launchArguments != null) return launchArguments;
        try {
            launchArguments = prepareArguments();
            return launchArguments;
        } catch (Exception error) {
            Log.e(TAG, "Cannot prepare packaged content", error);
            // The native entry reports an ordinary missing-project failure, without running unverified bytes.
            return new String[] { "--project", new File(getFilesDir(), "invalid-content/project.json").getPath() };
        }
    }

    private String[] prepareArguments() throws Exception {
        JSONObject manifest;
        try (InputStream stream = getAssets().open("install-manifest.json")) {
            manifest = new JSONObject(new String(readBounded(stream, 4 * 1024 * 1024), StandardCharsets.UTF_8));
        }
        if (manifest.getInt("format_version") != 1) throw new IOException("Unsupported content manifest");
        String contentId = manifest.getString("content_id");
        String storageId = manifest.getString("storage_id");
        if (!contentId.matches("[a-f0-9]{64}") || !storageId.matches("[a-f0-9]{24}"))
            throw new IOException("Invalid packaged content identity");
        File content = checked(new File(getFilesDir(), "content"), contentId);
        if (!content.mkdirs() && !content.isDirectory()) throw new IOException("Cannot create content directory");
        JSONArray files = manifest.getJSONArray("files");
        if (files.length() == 0 || files.length() > 16384) throw new IOException("Content file limit exceeded");
        Set<String> names = new HashSet<>();
        long total = 0;
        for (int index = 0; index < files.length(); ++index) {
            JSONObject entry = files.getJSONObject(index);
            String path = entry.getString("path");
            if (!(path.startsWith("game/") || path.startsWith("runtime/")) || !names.add(path))
                throw new IOException("Invalid or duplicate content path");
            long size = entry.getLong("bytes");
            String hash = entry.getString("sha256");
            if (size < 0 || size > MAX_FILE || (total += size) > MAX_BUNDLE || !hash.matches("[a-f0-9]{64}"))
                throw new IOException("Invalid content size or hash");
            File target = checked(content, path);
            if (target.isFile() && target.length() == size && hash.equals(fileHash(target))) continue;
            File parent = target.getParentFile();
            if (!parent.mkdirs() && !parent.isDirectory()) throw new IOException("Cannot create asset directory");
            File temporary = new File(parent, target.getName() + ".installing");
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            long copied = 0;
            try (InputStream in = getAssets().open(path); FileOutputStream out = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[65536];
                int count;
                while ((count = in.read(buffer)) != -1) {
                    copied += count;
                    if (copied > size) throw new IOException("Content size mismatch");
                    digest.update(buffer, 0, count);
                    out.write(buffer, 0, count);
                }
                out.getFD().sync();
            }
            if (copied != size || !hash.equals(hex(digest.digest()))) throw new IOException("Content hash mismatch");
            if (target.exists() && !target.delete()) throw new IOException("Cannot replace incomplete content");
            if (!temporary.renameTo(target)) throw new IOException("Cannot commit verified content");
        }
        String projectPath = manifest.getString("project_manifest");
        if (!names.contains(projectPath)) throw new IOException("Project manifest is not registered");
        File storage = checked(new File(getFilesDir(), "state"), storageId);
        if (!storage.mkdirs() && !storage.isDirectory()) throw new IOException("Cannot create save directory");
        ArrayList<String> arguments = new ArrayList<>();
        arguments.add("--project"); arguments.add(checked(content, projectPath).getPath());
        arguments.add("--runtime"); arguments.add(checked(content, "runtime").getPath());
        arguments.add("--storage"); arguments.add(storage.getPath());
        if (BuildConfig.DEBUG) {
            addDebugArguments(arguments, storage);
        }
        retainCurrentAndPreviousContent(content);
        Log.i(TAG, "content=" + contentId + " source_revision=" + manifest.getString("source_revision") +
              " packaged_revision=" + manifest.getString("packaged_revision") + " storage_id=" + storageId);
        return arguments.toArray(new String[0]);
    }

    private void addDebugArguments(ArrayList<String> result, File storage) throws Exception {
        String encoded = getIntent().getStringExtra("langmake_debug_args");
        if (encoded == null) return;
        if (encoded.length() > 16384) throw new IOException("Debug launch arguments exceed limit");
        JSONArray arguments = new JSONArray(new String(Base64.decode(encoded, Base64.DEFAULT), StandardCharsets.UTF_8));
        if (arguments.length() > 20) throw new IOException("Too many debug arguments");
        for (int index = 0; index < arguments.length(); ++index) {
            String option = arguments.getString(index);
            if (option.equals("--development") || option.equals("--fresh") || option.equals("--no-audio")) {
                result.add(option); continue;
            }
            if (!(option.equals("--frames") || option.equals("--steps") || option.equals("--ticks") ||
                  option.equals("--capture") || option.equals("--input") || option.equals("--output-dir")))
                throw new IOException("Unsupported debug option: " + option);
            if (++index >= arguments.length()) throw new IOException("Missing debug argument value");
            String value = arguments.getString(index);
            if (value.length() > 2048) throw new IOException("Debug argument exceeds limit");
            if (option.equals("--capture") || option.equals("--input") || option.equals("--output-dir"))
                value = checked(storage, value).getPath(); // Host supplies paths relative to this app's private storage.
            else if (!value.matches("[0-9,]+")) throw new IOException("Invalid numeric debug argument");
            result.add(option); result.add(value);
        }
    }

    private static File checked(File root, String relative) throws IOException {
        if (relative.isEmpty() || relative.startsWith("/") || relative.contains("\\") || relative.contains(":"))
            throw new IOException("Invalid relative content path");
        for (String part : relative.split("/")) if (part.isEmpty() || part.equals(".") || part.equals(".."))
            throw new IOException("Invalid relative path segment");
        File canonicalRoot = root.getCanonicalFile();
        File target = new File(canonicalRoot, relative).getCanonicalFile();
        if (!target.getPath().startsWith(canonicalRoot.getPath() + File.separator))
            throw new IOException("Content path escapes its root");
        return target;
    }

    private void retainCurrentAndPreviousContent(File current) {
        // Never traverse state/saves. Keep one previous verified/extracted revision for rollback.
        try {
            File root = new File(getFilesDir(), "content").getCanonicalFile();
            File[] candidates = root.listFiles(file -> file.isDirectory() && file.getName().matches("[a-f0-9]{64}"));
            if (candidates == null) return;
            Arrays.sort(candidates, Comparator.comparingLong(File::lastModified).reversed());
            boolean retainedPrevious = false;
            for (File candidate : candidates) {
                File checkedCandidate = checked(root, candidate.getName());
                if (checkedCandidate.equals(current.getCanonicalFile())) continue;
                if (!retainedPrevious) { retainedPrevious = true; continue; }
                Path target = checkedCandidate.toPath();
                // walkFileTree does not follow symbolic links; deletion is constrained to this revision.
                Files.walkFileTree(target, new SimpleFileVisitor<Path>() {
                    @Override public FileVisitResult visitFile(Path path, BasicFileAttributes attributes) throws IOException {
                        Files.delete(path); return FileVisitResult.CONTINUE;
                    }
                    @Override public FileVisitResult postVisitDirectory(Path path, IOException error) throws IOException {
                        if (error != null) throw error;
                        Files.delete(path); return FileVisitResult.CONTINUE;
                    }
                });
            }
            if (!current.setLastModified(System.currentTimeMillis())) Log.w(TAG, "Cannot update content access time");
        } catch (IOException error) {
            Log.w(TAG, "Old content cleanup deferred; save files are unaffected", error);
        }
    }

    private static byte[] readBounded(InputStream stream, int limit) throws IOException {
        java.io.ByteArrayOutputStream result = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[8192]; int count;
        while ((count = stream.read(buffer)) != -1) {
            if (result.size() + count > limit) throw new IOException("Manifest exceeds limit");
            result.write(buffer, 0, count);
        }
        return result.toByteArray();
    }

    private static String fileHash(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream stream = new FileInputStream(file)) {
            byte[] buffer = new byte[65536]; int count;
            while ((count = stream.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        return hex(digest.digest());
    }

    private static String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) result.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
        return result.toString();
    }
}
