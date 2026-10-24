#include "platform/FileHistoryStore.h"

#include <LittleFS.h>

#include "common/Log.h"

namespace device {

namespace {
constexpr const char* kMain = "/history.tsv";
constexpr const char* kMainTemp = "/history.tmp";
constexpr const char* kJournal = "/history-journal.tsv";
constexpr const char* kJournalTemp = "/history-journal.tmp";
constexpr const char* kLong = "/history-long.tsv";
constexpr const char* kLongTemp = "/history-long.tmp";
constexpr size_t kCompactWhenExtraLines = 2000;   // rewrite the long file once this many aged-out lines pile up
}  // namespace

bool FileHistoryStore::begin() {
    mounted_ = LittleFS.begin(/*formatOnFail=*/true);
    if (!mounted_) LOG_ERROR("History: could not mount the flash file system; history will not be kept across restarts.");
    return mounted_;
}

void FileHistoryStore::load(solar::HistoryService& history) {
    if (!mounted_) return;
    size_t lines = 0;
    bool journalFound = false;
    for (const char* path : {kMain, kLong, kJournal}) {
        File file = LittleFS.open(path, "r");
        if (!file) continue;
        if (path == kJournal) journalFound = true;
        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.endsWith("\r")) line.remove(line.length() - 1);
            history.loadLine(line.c_str());
            if (path == kLong) longLinesInFile_++;
            lines++;
        }
        file.close();
    }
    history.markLongSaved();   // what was loaded is already on flash
    history.markClean();
    LOG_INFO("History: restored %u lines from flash.", (unsigned)lines);
    if (journalFound) save(history);   // fold the journal into the full save so it is not lost on the next minute's rewrite
}

void FileHistoryStore::save(solar::HistoryService& history) {
    if (!mounted_) return;
    saveMain(history);
    saveLong(history);
}

void FileHistoryStore::saveJournal(solar::HistoryService& history) {
    if (!mounted_ || !history.journalPending()) return;
    File file = LittleFS.open(kJournalTemp, "w");
    if (!file) return;
    history.forEachJournalLine([&](const std::string& line) {
        file.print(line.c_str());
        file.print('\n');
    });
    file.close();
    LittleFS.remove(kJournal);
    LittleFS.rename(kJournalTemp, kJournal);
    history.markJournalSaved();
}

void FileHistoryStore::saveMain(solar::HistoryService& history) {
    File file = LittleFS.open(kMainTemp, "w");
    if (!file) {
        LOG_ERROR("History: could not write to flash.");
        return;
    }
    history.forEachLine([&](const std::string& line) {
        file.print(line.c_str());
        file.print('\n');
    });
    file.close();
    // Replace the old file only once the new one is complete, so a power cut mid-write loses nothing.
    LittleFS.remove(kMain);
    LittleFS.rename(kMainTemp, kMain);
    LittleFS.remove(kJournal);   // everything in it is now in the main file
    history.markClean();
}

void FileHistoryStore::saveLong(solar::HistoryService& history) {
    if (longLinesInFile_ > history.longRecordCount() + kCompactWhenExtraLines) {
        // Drop what has aged out: write everything still held to a new file and swap it in.
        File file = LittleFS.open(kLongTemp, "w");
        if (!file) return;
        size_t lines = 0;
        history.forEachLongLine([&](const std::string& line) {
            file.print(line.c_str());
            file.print('\n');
            lines++;
        }, /*onlyNew=*/false);
        file.close();
        LittleFS.remove(kLong);
        LittleFS.rename(kLongTemp, kLong);
        longLinesInFile_ = lines;
        history.markLongSaved();
        LOG_INFO("History: tidied the long-term file (%u records).", (unsigned)lines);
        return;
    }
    if (!history.longPending()) return;
    File file = LittleFS.open(kLong, "a");
    if (!file) {
        LOG_ERROR("History: could not add to the long-term file.");
        return;
    }
    history.forEachLongLine([&](const std::string& line) {
        file.print(line.c_str());
        file.print('\n');
        longLinesInFile_++;
    }, /*onlyNew=*/true);
    file.close();
    history.markLongSaved();
}

}  // namespace device
