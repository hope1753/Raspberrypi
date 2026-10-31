#ifndef MEP_COMMON_GPIO_HPP_
#define MEP_COMMON_GPIO_HPP_
#include <gpiod.h>
// One GPIO line, opened and released by the object itself.
//
// libgpiod v2 cleans up nothing on its own: a request must be released, and
// each of the three configuration objects below must be freed whether or not
// the request succeeded. Paying that cost once here is what lets every lab
// program stay short.
class Gpio {
    public:
        enum Mode {
        kOutputLow,
        kOutputHigh,
        kInput,
        kInputPullUp
    };
    Gpio(unsigned int line, Mode mode, const char* chip = "/dev/gpiochip0") : chip_(gpiod_chip_open(chip)), line_(line), request_(nullptr) {
        Reconfigure(mode);
    }
    ~Gpio() {
        Release();
        if (chip_ != nullptr) 
            gpiod_chip_close(chip_);
    }
    Gpio(const Gpio&) = delete;
    Gpio& operator=(const Gpio&) = delete;
    bool ok() const { return request_ != nullptr; }

    // Returns 0, 1, or -1 when the line is not usable.
    int Get() const {
        if (request_ == nullptr) return -1;
        const gpiod_line_value value = gpiod_line_request_get_value(request_, line_);
        return value == GPIOD_LINE_VALUE_ERROR ? -1 : static_cast<int>(value);
    }
    bool Set(int value) {
        if (request_ == nullptr) 
            return false;
        return gpiod_line_request_set_value(request_, line_, value != 0 ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0;
    }

    bool Reconfigure(Mode mode) {
        Release();
        if (chip_ == nullptr) 
            return false;
        gpiod_line_settings* settings = gpiod_line_settings_new();
        gpiod_line_config* lines = gpiod_line_config_new();
        gpiod_request_config* request = gpiod_request_config_new();
        if (settings != nullptr && lines != nullptr && request != nullptr &&Apply(settings, mode) && gpiod_line_config_add_line_settings(lines, &line_, 1, settings) == 0) {
            gpiod_request_config_set_consumer(request, "mep");
            request_ = gpiod_chip_request_lines(chip_, request, lines);
        }

        if (request != nullptr) 
            gpiod_request_config_free(request);
        if (lines != nullptr) 
            gpiod_line_config_free(lines);
        if (settings != nullptr) 
            gpiod_line_settings_free(settings);
            return request_ != nullptr;
    }
    void Release() {
        if (request_ != nullptr) {
            gpiod_line_request_release(request_);
            request_ = nullptr;
        }
    }
    private:

    static bool Apply(gpiod_line_settings* settings, Mode mode) {
        const bool output = mode == kOutputLow || mode == kOutputHigh;
        if (gpiod_line_settings_set_direction(settings, output ? GPIOD_LINE_DIRECTION_OUTPUT : GPIOD_LINE_DIRECTION_INPUT) != 0) {
            return false;
        }
        // v2 defaults to "as is", which leaves an input floating. The external
        // 10k is still the primary pull-up; this only keeps a loose one working.
        if (mode == kInputPullUp) {
            int res = gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
            return res == 0;
        }

        if (!output) return true;
            return gpiod_line_settings_set_output_value(settings, mode == kOutputHigh ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0;
        }
    gpiod_chip* chip_;
    unsigned int line_;
    gpiod_line_request* request_;
};
#endif // MEP_COMMON_GPIO_HPP_