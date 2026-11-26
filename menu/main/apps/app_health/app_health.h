/*
 * Launcher entry for the HealthWatch app in the ota_1 partition.
 *
 * Opening this entry marks ota_1 as the boot partition and restarts into HealthWatch.
 */
#pragma once
#include <mooncake.h>

class AppHealth : public mooncake::AppAbility {
public:
    AppHealth();

    void onCreate() override;
    void onOpen() override;
};
