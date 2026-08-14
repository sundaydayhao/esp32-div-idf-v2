#pragma once

#include <cstdint>

namespace AppUiTestHooks {

void resetToMenu();
void tapMain(int mainIdx);
void tapSubmenu(const char* groupName, int subIdx);
void openTopSubmenu(int mainIdx);
void openOtherLayer(uint8_t layer, int homeIdx, const char* name);

}  // namespace AppUiTestHooks

namespace UiSelfTest {

void pollAutoRun();
void runOnce();
void armExit(uint32_t ms);
void disarmExit();

}  // namespace UiSelfTest
