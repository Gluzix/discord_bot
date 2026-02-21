#include "CtrlMainServer.h"
// #include "Encoders/Mp3Encoder.h"

int main(int argc, char *argv[])
{
    CtrlMainServer server;
    server.run();
    // Mp3Encoder encoder;
    // encoder.PcmResample();
    // encoder.saveFileAsPcm();

    return 0;
}
