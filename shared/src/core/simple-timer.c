#include "core/simple-timer.h"
#include "core/system.h"


/**
 * @brief Initialize a simple software timer.
 *
 * This function arms the timer by calculating the absolute time
 * at which it should expire. The timer can operate either as:
 *  - a one-shot timer (auto_reset = false)
 *  - a periodic timer (auto_reset = true)
 *
 * @param timer       Pointer to the timer object to initialize
 * @param wait_time   Duration to wait before the timer expires
 *                    (same time units as system_get_time())
 * @param auto_reset  If true, the timer automatically reschedules itself
 *                    after expiring (periodic timer)
 */
void simple_timer_init(simple_timer_t* timer, uint64_t wait_time, bool auto_reset)
{
    timer->wait_time = wait_time;
    timer->target_time = system_get_ticks() + wait_time; // This should be set to the current time + wait_time
    timer->auto_reset = auto_reset;
    timer->timer_elapsed = false; // Initialize the timer as not elapsed
}



/**
 * @brief Check whether the timer has elapsed.
 *
 * This function compares the current system time with the timer's
 * target expiration time. If the timer has expired:
 *  - It returns true
 *  - If auto_reset is enabled, it schedules the next expiration
 *
 * Drift compensation is applied when auto_reset is enabled to prevent
 * gradual timing error accumulation if this function is called late.
 *
 * @param timer  Pointer to the timer to check
 * @return true  Timer has elapsed
 * @return false Timer has not yet elapsed
 */
bool simple_timer_has_elapsed(simple_timer_t* timer)
{
    uint64_t now = system_get_ticks();
    bool has_elapsed = now >= timer->target_time; // Check if the current time has reached or exceeded the target time
    
    if(timer->timer_elapsed) { // If the timer has already been marked as elapsed, we should not check it again until it's reset
        return false; // Return false if the timer was already marked as elapsed
    }
    
    if (has_elapsed) {// Check if the current time has reached or exceeded the target time
        uint64_t drift = now - timer->target_time; // Calculate how much time has passed since the target time
        if (timer->auto_reset) {
            timer->target_time = (now + timer->wait_time) - drift; // Reset the target time for the next interval
        }
        else {
            timer->timer_elapsed = true; // Mark the timer as elapsed
        } 
    }
    return has_elapsed;
}


/**
 * @brief Reset a simple software timer.
 *
 * This function re-arms the timer by setting its target time
 * to the current time plus the wait time.
 *
 * @param timer Pointer to the timer to reset
 */
void simple_timer_reset(simple_timer_t* timer)
{
    simple_timer_init(timer, timer->wait_time, timer->auto_reset); // Re-initialize the timer with the same wait time and auto-reset setting
}
