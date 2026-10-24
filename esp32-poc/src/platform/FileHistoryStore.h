#pragma once

#include "history/HistoryService.h"

namespace device {

/**
 * Keeps the history in files in the board's flash (LittleFS), so the graphs, the activity log and the year of history
 * survive a restart or a power cut.
 *
 *  - history.tsv: the last two days by the minute, the activity log and the daily totals. Rewritten every half hour,
 *    and only if something changed (about 130 KB).
 *  - history-long.tsv: the 5-minute and hourly records. Only ever added to at the end, so saving costs a few lines;
 *    it is rewritten from memory only now and then to drop what has aged out (a few times a year at most).
 *
 *  - history-journal.tsv: rewritten every minute with only what is new since the last full save (a few KB). Replayed
 *    on start-up, then folded into a full save and deleted.
 *
 * A power cut therefore loses at most the last minute.
 */
class FileHistoryStore {
public:
    /** Mounts the file system, formatting it the first time. */
    bool begin();

    void load(solar::HistoryService& history);
    /** Full save: rewrites the files and drops the journal. */
    void save(solar::HistoryService& history);
    /** Cheap save of what happened since the last full save (see HistoryService::forEachJournalLine). */
    void saveJournal(solar::HistoryService& history);

private:
    void saveMain(solar::HistoryService& history);
    void saveLong(solar::HistoryService& history);

    size_t longLinesInFile_ = 0;
    bool mounted_ = false;
};

}  // namespace device
