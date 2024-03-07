#include "util/time.hpp"

long operator-(timeval x, timeval y) {
    /* Perform the carry for the later subtraction by updating y. */
    if (x.tv_usec < y.tv_usec) {
        auto nsec = (y.tv_usec - x.tv_usec) / 1000000l + 1;
        y.tv_usec -= 1000000l * nsec;
        y.tv_sec += nsec;
    }
    if (x.tv_usec - y.tv_usec > 1000000l) {
        auto nsec = (x.tv_usec - y.tv_usec) / 1000000l;
        y.tv_usec += 1000000l * nsec;
        y.tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       tv_usec is certainly positive. */
    auto usecs = 1000000l * (x.tv_sec - y.tv_sec) + (x.tv_usec - y.tv_usec);
    if (x.tv_sec < y.tv_sec) {
        return -usecs;
    } else {
        return usecs;
    }
}

long operator-(timespec x, timespec y) {
    /* Perform the carry for the later subtraction by updating y. */
    if (x.tv_nsec < y.tv_nsec) {
        auto nsec = (y.tv_nsec - x.tv_nsec) / 1000000000l + 1;
        y.tv_nsec -= 1000000000l * nsec;
        y.tv_sec += nsec;
    }
    if (x.tv_nsec - y.tv_nsec > 1000000000l) {
        auto nsec = (x.tv_nsec - y.tv_nsec) / 1000000000l;
        y.tv_nsec += 1000000000l * nsec;
        y.tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       tv_usec is certainly positive. */
    auto nsecs = 1000000000l * (x.tv_sec - y.tv_sec) + (x.tv_nsec - y.tv_nsec);
    if (x.tv_sec < y.tv_sec) {
        return -nsecs;
    } else {
        return nsecs;
    }
}
