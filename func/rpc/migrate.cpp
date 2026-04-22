#include <rpc.h>

#include <faasm/migrate.h>
#include <faasm/time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int checkEvery = 1;
int numLoops = 0;

// Outer wrapper and re-entry point after migration
void doBenchmark(int nLoops)
{
    bool mustCheck = (nLoops == numLoops);

    Rpc_ChannelId channelId = 0;
    int createStatus =
      Rpc_ChannelCreate("faabric://127.0.0.1", &channelId);

    if (createStatus != Rpc_StatusCode::OK) {
        printf("Rpc_ChannelCreate failed with status %d\n", createStatus);
        return;
    }

    double timeStartSec = 0.0;
    double timeEndSec = 0.0;

    printf("time now: %f\n", faasm::getSecondsSinceEpoch());
    printf("%i loops to go\n", nLoops);

    // Tiny request payload for unary call
    const char* payload = "hello";
    const uint8_t* reqBuf = reinterpret_cast<const uint8_t*>(payload);
    int32_t reqLen = static_cast<int32_t>(strlen(payload));

    for (int i = 0; i < nLoops; i++) {
        if (i % (nLoops / 10 == 0 ? 1 : nLoops / 10) == 0) {
            printf("Starting iteration %i/%i\n", i, nLoops);
        }

        // Unary RPC call (the method string must exist in your RPC service)
        uint8_t* respBuf = nullptr;
        int32_t respLen = 0;
        int callStatus = Rpc_UnaryCall(
          channelId,
          "ping", // Replace with your real method name
          reqBuf,
          reqLen,
          &respBuf,
          &respLen);

        if (callStatus != Rpc_StatusCode::OK) {
            printf("Rpc_UnaryCall failed with status %d\n", callStatus);
        } else {
            printf("Rpc_UnaryCall ok, response length=%d\n", respLen);
        }

        // Runtime allocates response memory for guest; free after use
        if (respBuf != nullptr) {
            free(respBuf);
            respBuf = nullptr;
        }

        if (!mustCheck && i % checkEvery == 1 && i / checkEvery > 0) {
            timeEndSec = faasm::getSecondsSinceEpoch();
            printf("Spent %f sec migrating\n", timeEndSec - timeStartSec);
        }

        if (mustCheck && i % checkEvery == 0 && i / checkEvery > 0) {
            mustCheck = false;
            printf("Checking for migration at iteration %i/%i\n", i, nLoops);

            timeStartSec = faasm::getSecondsSinceEpoch();
            printf("Migration start time: %f\n", timeStartSec);
            printf("Remaining loops arg: %i\n", nLoops - i - 1);

            __faasm_migrate_point(&doBenchmark, (nLoops - i - 1));
        }
    }

    int closeStatus = Rpc_ChannelClose(channelId);
    if (closeStatus != Rpc_StatusCode::OK) {
        printf("Rpc_ChannelClose failed with status %d\n", closeStatus);
    }
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        printf("Must provide two input arguments: <check_period> <num_loops>\n");
        return 1;
    }

    int checkEveryIn = atoi(argv[1]);
    int numLoopsIn = atoi(argv[2]);

    numLoops = numLoopsIn;
    checkEvery = (int)(numLoops * ((float)checkEveryIn / 10.0f));
    if (checkEvery <= 0) {
        checkEvery = 1;
    }

    printf("Starting RPC migration checking at iter %i/%i\n", checkEvery, numLoops);

    doBenchmark(numLoops);

    printf("RPC migration benchmark finished successfully\n");
    return 0;
}