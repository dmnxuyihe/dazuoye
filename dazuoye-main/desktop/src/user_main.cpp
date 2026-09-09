#include "launch.h"
#include "user_window.h"
int main(int argc, char **argv) {
    return launch<UserWindow>(
        argc, argv, "User",
        {"home", "map", "station", "charging", "stats", "schedule", "history", "profile"});
}
