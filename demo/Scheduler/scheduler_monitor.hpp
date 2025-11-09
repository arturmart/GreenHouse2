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
#include <algorithm> // max, min
#include <utility>   // pair

// ------------------------
// TerminalMonitor (self-contained)
// ------------------------
class TerminalMonitor {
public:
    using Ms = std::chrono::milliseconds;

    // period      — период обновления «кадра»
    // historyCols — ширина истории (сколько столбцов на экране)
    // colWidth    — ширина одной ячейки вывода
    // row_offset  — строка, с которой начинается окно монитора (1 = верх)
    // height      — высота окна монитора (строк)
    //
    // Совместимость: старая сигнатура на 3 аргумента продолжает работать,
    // т.к. row_offset и height имеют дефолтные значения.
    TerminalMonitor(Ms period = Ms(200),
                    int historyCols = 60,
                    int colWidth    = 7,
                    int row_offset  = 1,
                    int height      = 10)
        : refresh_(period),
          historyCols_(std::max(5, historyCols)),
          colWidth_(std::max(2, colWidth)),
          row_off_(std::max(1, row_offset)),
          height_(std::max(4, height)) {}

    // Можно поменять окно после создания
    void setWindow(int row_offset, int height) {
        row_off_ = std::max(1, row_offset);
        height_  = std::max(4, height);
    }

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
        std::cout.flush();
    }

private:
    // ===== ANSI helpers (локальные, адресный вывод только в своей области) =====
    static inline void ansi_goto(int row, int col) { std::cout << "\x1b[" << row << ';' << col << 'H'; }
    static inline void ansi_reset()                { std::cout << "\x1b[0m"; }
    static inline void ansi_bg256(int code)        { std::cout << "\x1b[48;5;" << code << 'm'; }
    static inline void ansi_fg256(int code)        { std::cout << "\x1b[38;5;" << code << 'm'; }

    // Очистка прямоугольника окна монитора. Никакого глобального clear!
    void clearBox() {
        const int colsToClear = 5 + historyCols_ * colWidth_ + 4;
        for (int r = 0; r < height_; ++r) {
            ansi_goto(row_off_ + r, 1);
            for (int c = 0; c < colsToClear; ++c) std::cout << ' ';
        }
    }

    // ===== Данные клетки таймлайна =====
    struct Cell {
        std::string text = ".";                    // ".", "*", либо id в строке
        std::optional<Scheduler::TaskId> id{};     // id для окраски (если есть)
    };

    // Подгон текста к ширине
    static std::string fitCell(const std::string& s, int w) {
        if ((int)s.size() == w) return s;
        if ((int)s.size() <  w) { std::string t = s; t.resize(w, ' '); return t; }
        return s.substr(0, w);
    }

    // Небольшая палитра приятных фонов (ANSI-256)
    static const std::vector<int>& palette() {
        static const std::vector<int> k = { 226, 196, 46, 21, 201, 51, 208, 93, 118, 33 };
        return k;
    }
    static int colorForId(Scheduler::TaskId id) {
        const auto& p = palette();
        return p[static_cast<size_t>(id % p.size())];
    }
    // Грубая эвристика контраста: «светлым» фонам — чёрный, иначе белый
    static int fgForBg(int bg256) {
        return (bg256 >= 186 ? 16 : 15);
    }

    // Один кадр отрисовки в своей области
    void drawBoard() {
        int row = row_off_;

        // Заголовок
        ansi_goto(row++, 1);
        std::cout << "Thread-pool timeline (rows=workers, cols=ticks). "
                     "Cell = task id (* if many, '.' if idle)\n";

        // Статус
        if (row <= row_off_ + height_ - 1) {
            ansi_goto(row++, 1);
            std::cout << "Workers observed: " << workerRows_
                      << "  |  Ctrl+C to stop\n";
        }

        // Шапка колонок
        if (row <= row_off_ + height_ - 1) {
            ansi_goto(row++, 1);
            std::cout << "     ";
            for (int c = 0; c < historyCols_; ++c) {
                std::ostringstream os; os << std::setw(colWidth_ - 1) << c;
                std::cout << fitCell(os.str(), colWidth_);
            }
            std::cout << "\n";
        }

        // Сколько строк осталось под воркеры
        int rows_left    = row_off_ + height_ - row;
        int rows_to_draw = std::min(workerRows_, std::max(0, rows_left));

        for (int r = 0; r < rows_to_draw; ++r) {
            ansi_goto(row++, 1);
            std::ostringstream os; os << "W" << r;
            std::cout << std::left << std::setw(5) << os.str();

            for (int c = 0; c < historyCols_; ++c) {
                const Cell& cell = grid_[r][c];

                if (cell.text == ".") {
                    std::cout << fitCell(".", colWidth_);
                    continue;
                }
                if (cell.text == "*") {
                    ansi_bg256(208); // тёплый оранжевый
                    ansi_fg256(16);  // чёрный
                    std::cout << fitCell("*", colWidth_);
                    ansi_reset();
                    continue;
                }
                int bg = 237, fg = 15;
                if (cell.id) { bg = colorForId(*cell.id); fg = fgForBg(bg); }
                ansi_bg256(bg);
                ansi_fg256(fg);
                std::cout << fitCell(cell.text, colWidth_);
                ansi_reset();
            }
            std::cout << "\n";
        }
        std::cout.flush();
    }

    // Основной цикл
    void loop() {
        while (running_) {
            // 1) Актуализируем число воркеров (расширяем сетку при росте)
            int observed = std::max(1, sched_->workersObserved());
            if (observed > workerRows_) {
                grid_.resize(observed, std::vector<Cell>(historyCols_, Cell{}));
                workerRows_ = observed;
            }

            // 2) Сдвиг истории влево
            for (auto& row : grid_) {
                for (int c = 0; c < historyCols_ - 1; ++c) row[c] = row[c + 1];
                row[historyCols_ - 1] = Cell{}; // текущий столбец пустой
            }

            // 3) Помечаем выполняющиеся задачи в «текущем столбце»
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
                    // коллизия — несколько задач на одном воркере
                    grid_[r][historyCols_ - 1].text = "*";
                    grid_[r][historyCols_ - 1].id.reset();
                }
                counts[r]++;
            }

            // 4) Рисуем только в своём окне
            clearBox();
            drawBoard();

            // 5) Пауза до следующего кадра
            std::this_thread::sleep_for(refresh_);
        }
    }

private:
    Scheduler* sched_ = nullptr;

    Ms  refresh_;
    int historyCols_;
    int colWidth_;

    int row_off_;
    int height_;

    std::atomic<bool> running_{false};
    std::thread       thr_;

    int workerRows_ = 1;
    std::vector<std::vector<Cell>> grid_;
};
