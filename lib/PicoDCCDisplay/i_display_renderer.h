/* lib/PicoDCCDisplay/i_display_renderer.h */
#ifndef I_DISPLAY_RENDERER_H
#define I_DISPLAY_RENDERER_H

#include <stdint.h>

// Forward declarations
struct TrackStatus;

/**
 * @brief Interface for display rendering abstraction
 * 
 * This interface decouples the business logic (PicoDCCDisplay) from the
 * rendering implementation (LVGL). Allows for clean testing without hardware
 * or LVGL dependencies.
 */
class IDisplayRenderer {
public:
    virtual ~IDisplayRenderer() = default;
    
    /**
     * @brief Initialize the renderer (LVGL setup, screen creation, etc.)
     * @return true if initialization succeeded
     */
    virtual bool init() = 0;
    
    /**
     * @brief Show a test color pattern (boot sequence phase 1)
     */
    virtual void showTestPattern() = 0;
    
    /**
     * @brief Create and show the diagnostic screen (boot sequence phase 2)
     */
    virtual void showDiagnosticScreen() = 0;
    
    /**
     * @brief Update the diagnostic screen with current track status
     * @param status Current track status data
     */
    virtual void updateDiagnosticScreen(const TrackStatus& status) = 0;
    
    /**
     * @brief Perform periodic renderer updates (LVGL timer handling, etc.)
     * Must be called regularly (e.g., 10Hz) for proper operation
     */
    virtual void tick() = 0;
    
    /**
     * @brief Set the controller reference for button callbacks
     * @param controller Pointer to the DCC controller
     */
    virtual void setController(class PicoDccController* controller) = 0;
    
    /**
     * @brief Show the diagnostic log viewer screen
     * Displays log entries from the diagnostic buffer
     */
    virtual void showLogScreen() = 0;
    
    /**
     * @brief Update the log screen with current entries
     * Refreshes the log table/list with latest diagnostic messages
     */
    virtual void updateLogScreen() = 0;
    
    /**
     * @brief Show the settings screen
     * Provides access to maintenance mode and configuration
     */
    virtual void showSettingsScreen() = 0;
    
    /**
     * @brief Show the maintenance mode entry modal
     * Displays safety checklist for entering maintenance mode
     * @return true if user confirmed entry, false if cancelled
     */
    virtual bool showMaintenanceModeEntryModal() = 0;
    
    /**
     * @brief Show the maintenance mode screen
     * Displays save button and configuration status
     */
    virtual void showMaintenanceModeScreen() = 0;
    
    /**
     * @brief Update the maintenance mode screen with current status
     * Refreshes unsaved changes indicator
     */
    virtual void updateMaintenanceModeScreen(bool has_unsaved) = 0;
    
    /**
     * @brief Show unsaved changes warning modal on exit
     * @return true if user wants to discard, false to cancel exit
     */
    virtual bool showUnsavedChangesModal() = 0;
};

#endif // I_DISPLAY_RENDERER_H
