#include "state.h"

AppState app;

bool SavedDate::isYesterdayOf(const SavedDate& next) const {
    if (!valid() || !next.valid()) return false;

    static const uint8_t dim[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int16_t d = day, m = month, y = year;

    d++;
    bool leap = (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
    uint8_t daysInMonth = (m == 2 && leap) ? 29 : dim[m];
    if (d > daysInMonth) { d = 1; m++; }
    if (m > 12)          { m = 1; y++; }

    return d == next.day && m == next.month && y == next.year;
}
