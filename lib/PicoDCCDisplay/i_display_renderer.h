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
};

#endif // I_DISPLAY_RENDERER_H
