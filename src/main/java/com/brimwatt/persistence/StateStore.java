package com.brimwatt.persistence;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Optional;

/** Reads and writes the state file. Writes go to a temporary file first, so a crash never leaves half a file. */
public class StateStore {

    private static final Logger logger = LoggerFactory.getLogger(StateStore.class);

    private final Path path;
    private final ObjectMapper mapper = new ObjectMapper()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .enable(SerializationFeature.INDENT_OUTPUT);

    public StateStore(Path path) {
        this.path = path;
    }

    public Path path() { return path; }

    private boolean damaged;

    /** True if the last {@link #load()} found a state file that could not be read, and no usable backup either. */
    public boolean lastLoadWasDamaged() { return damaged; }

    private Path backupPath() { return path.resolveSibling(path.getFileName() + ".bak"); }

    /** Empty if there is no file yet or it cannot be read; a bad file never stops the app. */
    public Optional<PersistedState> load() {
        damaged = false;
        if (!Files.exists(path)) return Optional.empty();
        try {
            return Optional.ofNullable(mapper.readValue(path.toFile(), PersistedState.class));
        } catch (Exception e) {
            logger.warn("State file {} is unreadable: {}", path, e.getMessage());
        }
        // The previous good copy, kept by write(), is better than starting from the defaults.
        Path backup = backupPath();
        if (Files.exists(backup)) {
            try {
                Optional<PersistedState> saved = Optional.ofNullable(mapper.readValue(backup.toFile(), PersistedState.class));
                logger.warn("Used the backup {} instead.", backup);
                return saved;
            } catch (Exception e) {
                logger.warn("The backup {} is unreadable too: {}", backup, e.getMessage());
            }
        }
        damaged = true;
        return Optional.empty();
    }

    public String toJson(PersistedState state) {
        try {
            return mapper.writeValueAsString(state);
        } catch (IOException e) {
            throw new IllegalStateException("Failed to serialize state", e);
        }
    }

    public void write(String json) throws IOException {
        Path parent = path.toAbsolutePath().getParent();
        if (parent != null) Files.createDirectories(parent);
        Path temp = path.resolveSibling(path.getFileName() + ".tmp");
        Files.writeString(temp, json);
        if (Files.exists(path)) {   // keep the previous good copy, in case the new file is ever damaged
            try { Files.copy(path, backupPath(), StandardCopyOption.REPLACE_EXISTING); } catch (IOException ignored) { }
        }
        try {
            Files.move(temp, path, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException e) {
            Files.move(temp, path, StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
