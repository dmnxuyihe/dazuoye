#include "admin_window.h"
#include "launch.h"
int main(int argc, char **argv) {
    return launch<AdminWindow>(argc, argv, "Admin",
                               {"dashboard", "station", "trips", "history", "forecast"});
}
