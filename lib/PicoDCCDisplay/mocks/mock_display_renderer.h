/* lib/PicoDCCDisplay/mocks/mock_display_renderer.h */
#ifndef MOCK_DISPLAY_RENDERER_H
#define MOCK_DISPLAY_RENDERER_H

#include "../i_display_renderer.h"

/**
 * @brief Mock implementation of the display renderer for unit testing
 * 
 * This mock allows testing of PicoDCCDisplay business logic without
 * any hardware or LVGL dependencies. Tracks method calls for verification.
 */
class MockDisplayRenderer : public IDisplayRenderer {
public:
    MockDisplayRenderer();
    ~MockDisplayRenderer() override = default;
    
    bool init() override;
    void showTestPattern() override;
    void showDiagnosticScreen() override;
    void updateDiagnosticScreen(const TrackStatus& status) override;
    void showLogScreen() override;
    void updateLogScreen() override;
    void tick() override;
    void setController(class PicoDccController* controller) override;
    
    // Layout Maintenance Mode UI (stubs for test mode)
    void showSettingsScreen() override;
    bool showMaintenanceModeEntryModal() override;
    void showMaintenanceModeScreen() override;
    void updateMaintenanceModeScreen(bool has_unsaved_changes) override;
    bool showUnsavedChangesModal() override;
    
    // Test inspection methods
    bool wasInitCalled() const { return init_called_; }
    bool wasTestPatternShown() const { return test_pattern_shown_; }
    bool wasDiagnosticScreenShown() const { return diagnostic_screen_shown_; }
    bool wasLogScreenShown() const { return log_screen_shown_; }
    int getUpdateCount() const { return update_count_; }
    int getLogUpdateCount() const { return log_update_count_; }
    int getTickCount() const { return tick_count_; }
    const TrackStatus* getLastStatus() const { return last_status_; }
    class PicoDccController* getController() const { return controller_ref_; }
    
    // Test control methods
    void reset();
    void setInitResult(bool result) { init_result_ = result; }
    
private:
    bool init_called_;
    bool init_result_;
    bool test_pattern_shown_;
    bool diagnostic_screen_shown_;
    bool log_screen_shown_;
    int update_count_;
    int log_update_count_;
    int tick_count_;
    const TrackStatus* last_status_;
    class PicoDccController* controller_ref_;
};

#endif // MOCK_DISPLAY_RENDERER_H
