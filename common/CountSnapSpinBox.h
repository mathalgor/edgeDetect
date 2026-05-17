#ifndef COUNT_SNAP_SPIN_BOX_H
#define COUNT_SNAP_SPIN_BOX_H

#include <QSpinBox>
#include <cstdlib>
#include <functional>

// QSpinBox that steps to the next value where a user-supplied counting
// function returns a different result. Useful when stepping by 1 produces
// long runs of identical pixel counts and the user wants to skip them.
class CountSnapSpinBox : public QSpinBox {
public:
    using CountFn = std::function<int(int)>;
    CountSnapSpinBox(QWidget* p, CountFn fn) : QSpinBox(p), fn_(std::move(fn)) {}
    void setCountFn(CountFn fn) { fn_ = std::move(fn); }

protected:
    void stepBy(int steps) override {
        if (steps == 0 || !fn_) { QSpinBox::stepBy(steps); return; }
        const int dir = steps > 0 ? 1 : -1;
        const int n = std::abs(steps);
        int v = value();
        const int lo = minimum(), hi = maximum();
        for (int i = 0; i < n; ++i) {
            const int c0 = fn_(v);
            int next = v + dir;
            while (next >= lo && next <= hi && fn_(next) == c0) next += dir;
            if (next < lo) next = lo;
            else if (next > hi) next = hi;
            if (next == v) break;
            v = next;
        }
        setValue(v);
    }

private:
    CountFn fn_;
};

#endif
