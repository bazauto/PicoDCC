/* lib/PicoDCCDisplay/mocks/lcd_driver_mock.cpp */
#ifdef TEST_BUILD

#include "../lcd_driver.h"

// Mock implementation for TEST_BUILD mode
LcdDriver::LcdDriver() : initialized_(false) {}
LcdDriver::~LcdDriver() {}

bool LcdDriver::init() { 
    initialized_ = true;
    return true; 
}

void LcdDriver::reset() {}
void LcdDriver::writeCommand(uint8_t cmd) {}
void LcdDriver::writeData(uint8_t data) {}
void LcdDriver::writeData(const uint8_t* buffer, size_t len) {}
void LcdDriver::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {}
void LcdDriver::fillScreen(uint16_t color) {}
void LcdDriver::pushPixels(const uint16_t* pixels, size_t count) {}
void LcdDriver::displayOn() {}
void LcdDriver::displayOff() {}
void LcdDriver::sleep() {}
void LcdDriver::wakeup() {}
void LcdDriver::initGPIO() {}
void LcdDriver::initSPI() {}
void LcdDriver::sendInitSequence() {}

#endif // TEST_BUILD
