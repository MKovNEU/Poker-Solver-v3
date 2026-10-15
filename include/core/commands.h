#pragma once

#include <cctype>
#include <condition_variable>
#include <deque>
#include <istream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// command lines for the --serve protocols, read on their own thread so a "stop" gets through while a solve is busy
class Commands {
public:
    explicit Commands(std::istream& in) : state_(std::make_shared<State>()) {
        in.tie(nullptr); // reading must not flush the output from the reader thread
        std::thread([state = state_, &in] {
            std::string line;
            while (std::getline(in, line)) {
                while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
                std::lock_guard<std::mutex> lock(state->mutex);
                if (line == "stop") state->stop = true;
                else state->lines.push_back(line);
                state->ready.notify_one();
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            state->eof = state->stop = true; // the UI went away, finish up
            state->ready.notify_one();
        }).detach();
    }

    bool next(std::string& line) {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->ready.wait(lock, [&] { return !state_->lines.empty() || state_->eof; });
        if (state_->lines.empty()) return false;
        line = std::move(state_->lines.front());
        state_->lines.pop_front();
        return true;
    }

    bool stop_requested() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->stop;
    }

    void clear_stop() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stop = state_->eof;
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::string> lines;
        bool stop = false, eof = false;
    };
    std::shared_ptr<State> state_; // shared with the reader thread, which outlives the session
};
