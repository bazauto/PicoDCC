/* lib/PicoDCCDisplay/mocks/mock_display_renderer.cpp */
#include "mock_display_renderer.h"
#include "../pico_dcc_display.h"

MockDisplayRenderer::MockDisplayRenderer() 
    : init_called_(false)
    , init_result_(true)
    , test_pattern_shown_(false)
    , diagnostic_screen_shown_(false)
    , log_screen_shown_(false)
    , update_count_(0)
    , log_update_count_(0)
    , tick_count_(0)
    , last_status_(nullptr)
    , controller_ref_(nullptr)
{
}

bool MockDisplayRenderer::init() {
    init_called_ = true;
    return init_result_;
}

void MockDisplayRenderer::showTestPattern() {
    test_pattern_shown_ = true;
}

void MockDisplayRenderer::showDiagnosticScreen() {
    diagnostic_screen_shown_ = true;
}

void MockDisplayRenderer::updateDiagnosticScreen(const TrackStatus& status) {
    last_status_ = &status;
    update_count_++;
}

void MockDisplayRenderer::showLogScreen() {
    log_screen_shown_ = true;
}

void MockDisplayRenderer::updateLogScreen() {
    log_update_count_++;
}

void MockDisplayRenderer::tick() {
    tick_count_++;
}

void MockDisplayRenderer::setController(class PicoDccController* controller) {
    controller_ref_ = controller;
}

void MockDisplayRenderer::showSettingsScreen() {
    // Stub: No-op for test mode
}

bool MockDisplayRenderer::showMaintenanceModeEntryModal() {
    // Stub: Return false (user declined) for test mode
    return false;
}

void MockDisplayRenderer::showMaintenanceModeScreen() {
    // Stub: No-op for test mode
}

void MockDisplayRenderer::updateMaintenanceModeScreen(bool has_unsaved_changes) {
    // Stub: No-op for test mode
    (void)has_unsaved_changes;
}

bool MockDisplayRenderer::showUnsavedChangesModal() {
    // Stub: Return false (user declined) for test mode
    return false;
}

void MockDisplayRenderer::reset() {
    init_called_ = false;
    test_pattern_shown_ = false;
    diagnostic_screen_shown_ = false;
    log_screen_shown_ = false;
    update_count_ = 0;
    log_update_count_ = 0;
    tick_count_ = 0;
    last_status_ = nullptr;
    controller_ref_ = nullptr;
}
