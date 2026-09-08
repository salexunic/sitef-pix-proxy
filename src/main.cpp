#include "pix_core.h"

int runServer(PixCore& core); // definida em server.cpp

int main() {
    PixCore core;
    return runServer(core);
}
