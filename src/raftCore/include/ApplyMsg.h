#pragma once

#include <string>

class ApplyMsg {
public:
    bool CommandValid;
    std::string Command;
    int CommandIndex;

    bool SnapshotValid;
    std::string Snapshot;
    int SnapshotTerm;
    int SnapshotIndex;


    ApplyMsg()
        : CommandValid(false),
            Command(),
            CommandIndex(-1),
            SnapshotValid(false),
            Snapshot(),
            SnapshotTerm(-1),
            SnapshotIndex(-1) {}
};