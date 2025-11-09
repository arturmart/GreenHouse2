#pragma once
#include "Scheduler.hpp"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class TerminalMonitor {
public:
    using Ms = std::chrono::milliseconds;

    // refresh — период обновления «кадра»
    // historyCols — ширина истории (сколько столбцов на экране)
    // colWidth — ширина одной ячейки
    TerminalMonitor(Ms refresh = Ms(200), int historyCols = 60, int colWidth = 7)
        : refresh_(refresh), historyCols_(historyCols), colWidth_(colWidth) {}

    // Запуск в отдельном потоке
    void start(Scheduler& sched) {
        if (running_.exchange(true)) return; // уже запущен
        sched_ = &sched;
        workerRows_ = std::max(1, sched_->workersObserved());
        grid_.assign(workerRows_, std::vector<Cell>(historyCols_, Cell{}));
        thr_ = std::thread(&TerminalMonitor::loop, this);
    }

    // Остановка и join
    void stop() {
        if (!running_.exchange(false)) return;
        if (thr_.joinable()) thr_.join();
        ansiShowCur();
        std::cout << std::endl;
    }

private:
    // ===== Модель ячейки =====
    struct Cell {
        // text показываем (обычно id как строка или "*"), "." — пусто
        std::string text = ".";
        // task id (для цвета); nullopt если пусто или "*"
        std::optional<Scheduler::TaskId> id{};
    };

    // ===== отрисовка =====
    static inline void ansiClear()   { std::cout << "\x1b[2J"; }
    static inline void ansiHome()    { std::cout << "\x1b[H"; }
    static inline void ansiHideCur() { std::cout << "\x1b[?25l"; }
    static inline void ansiShowCur() { std::cout << "\x1b[?25h"; }
    static inline void ansiReset()   { std::cout << "\x1b[0m";  }

    static inline void ansiBg256(int code) { std::cout << "\x1b[48;5;" << code << "m"; }
    static inline void ansiFg256(int code) { std::cout << "\x1b[38;5;" << code << "m"; }

    static std::string fitCell(const std::string& s, int w) {
        if ((int)s.size() == w) return s;
        if ((int)s.size() <  w) {
            std::string t = s;
            t.resize(w, ' ');
            return t;
        }
        return s.substr(0, w);
    }

    // Палитра приятных фонов (ANSI 256-color). Стабильная по индексу.
    // Подбирал контрастные, без «ядовитых» и слишком тёмных.
    static const std::vector<int>& palette() {
        static const std::vector<int> k = {
            226, // yellow
            196, // bright red
            46,  // bright green
            21,  // blue
            201, // magenta / purple
            51,  // cyan / turquoise
            208, // orange
            93,  // pink
            118, // lime
            33   // sky blue
        };
        return k;
    }

    // Маппинг id -> bg color (через палитру)
    static int colorForId(Scheduler::TaskId id) {
        const auto& p = palette();
        return p[static_cast<size_t>(id % p.size())];
    }

    // Выбор читабельного цвета текста (белый/чёрный) для заданного фона 256-color.
    // Грубая эвристика: светлым фонам — чёрный fg (16), тёмным — белый fg (15).
    static int fgForBg(int bgCode) {
        // Простой порог: более «высокие» коды в палитре у нас обычно светлее
        // но надёжнее — вручную: «светлым» даём чёрный
        // Возьмём правило: если bg из набора очень светлых — чёрный, иначе белый
        static const std::unordered_set<int> blackFgPreferred = {
            225, 219, 213, 207, 201, 195, 189, 183, 177, 171, 165, 159, 153, 148, 190
        };
        return (blackFgPreferred.count(bgCode) ? 16 : 15);
    }

    // Цвет для «несколько задач на воркере одновременно»
    static int multiBgColor() { return 208; } // тёплый оранжевый
    static int multiFgColor() { return 16;  } // чёрный текст поверх него

    void drawBoard() {
        ansiHome();
        std::cout << "Thread-pool timeline (rows=workers, cols=ticks). "
                     "Cell = task id (or * if many, '.' if idle)\n";
        std::cout << "Workers observed: " << workerRows_ << " | Press Ctrl+C to stop\n\n";

        // header
        std::cout << "     ";
        for (int c = 0; c < historyCols_; ++c) {
            std::ostringstream os; os << std::setw(colWidth_ - 1) << c;
            std::cout << fitCell(os.str(), colWidth_);
        }
        std::cout << "\n";

        // rows
        for (int r = 0; r < workerRows_; ++r) {
            std::ostringstream os; os << "W" << r;
            std::cout << std::left << std::setw(5) << os.str();
            for (int c = 0; c < historyCols_; ++c) {
                const Cell& cell = grid_[r][c];
                if (cell.text == ".") {
                    // пустая ячейка — просто печать без цвета
                    std::cout << fitCell(".", colWidth_);
                    continue;
                }
                if (cell.text == "*") {
                    // несколько задач — спец. цвет
                    ansiBg256(multiBgColor());
                    ansiFg256(multiFgColor());
                    std::cout << fitCell("*", colWidth_);
                    ansiReset();
                    continue;
                }
                // иначе это id
                int bg = 237; // запасной фон
                int fg = 15;  // белый
                if (cell.id.has_value()) {
                    bg = colorForId(*cell.id);
                    fg = fgForBg(bg);
                }
                ansiBg256(bg);
                ansiFg256(fg);
                std::cout << fitCell(cell.text, colWidth_);
                ansiReset();
            }
            std::cout << "\n";
        }
        std::cout.flush();
    }

    void loop() {
        ansiHideCur();
        ansiClear();

        while (running_) {
            // 1) актуализируем число воркеров
            int observed = std::max(1, sched_->workersObserved());
            if (observed > workerRows_) {
                grid_.resize(observed, std::vector<Cell>(historyCols_, Cell{}));
                workerRows_ = observed;
            }

            // 2) сдвиг истории
            for (auto& row : grid_) {
                for (int c = 0; c < historyCols_ - 1; ++c) row[c] = row[c + 1];
                row[historyCols_ - 1] = Cell{}; // заполняем текущий столбец пустыми
            }

            // 3) отметим выполняющиеся сейчас задачи
            auto runningNow = sched_->listRunningDetailed();
            std::vector<int> counts(workerRows_, 0);
            for (const auto& info : runningNow) {
                const int r = info.workerIndex;
                if (r < 0 || r >= workerRows_) continue;

                if (counts[r] == 0) {
                    Cell cell;
                    cell.text = std::to_string(info.id);
                    cell.id   = info.id;
                    grid_[r][historyCols_ - 1] = std::move(cell);
                } else {
                    // при коллизии ставим "*"
                    grid_[r][historyCols_ - 1].text = "*";
                    grid_[r][historyCols_ - 1].id.reset();
                }
                counts[r]++;
            }

            // 4) отрисовка
            drawBoard();

            // 5) пауза до следующего кадра
            std::this_thread::sleep_for(refresh_);
        }
    }

private:
    Scheduler* sched_ = nullptr;

    Ms  refresh_;
    int historyCols_;
    int colWidth_;

    std::atomic<bool> running_{false};
    std::thread       thr_;

    int workerRows_ = 1;
    std::vector<std::vector<Cell>> grid_;
};
